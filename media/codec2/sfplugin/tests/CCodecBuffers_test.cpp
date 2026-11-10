/*
 * Copyright 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "CCodecBuffers.h"

#include <gtest/gtest.h>

#include <codec2/hidl/client.h>
#include <media/stagefright/MediaCodecConstants.h>
#include <media/stagefright/foundation/ABuffer.h>

#include <C2BlockInternal.h>
#include <C2PlatformSupport.h>
#include <Codec2BufferUtils.h>
#include <Codec2Mapper.h>

namespace android {

static std::shared_ptr<RawGraphicOutputBuffers> GetRawGraphicOutputBuffers(
        int32_t width, int32_t height) {
    std::shared_ptr<RawGraphicOutputBuffers> buffers =
        std::make_shared<RawGraphicOutputBuffers>("test");
    sp<AMessage> format{new AMessage};
    format->setInt32(KEY_WIDTH, width);
    format->setInt32(KEY_HEIGHT, height);
    buffers->setFormat(format);
    return buffers;
}

TEST(RawGraphicOutputBuffersTest, ChangeNumSlots) {
    constexpr int32_t kWidth = 3840;
    constexpr int32_t kHeight = 2160;

    std::shared_ptr<RawGraphicOutputBuffers> buffers =
        GetRawGraphicOutputBuffers(kWidth, kHeight);

    std::shared_ptr<C2BlockPool> pool;
    ASSERT_EQ(OK, GetCodec2BlockPool(C2BlockPool::BASIC_GRAPHIC, nullptr, &pool));

    // Register 4 buffers
    std::vector<sp<MediaCodecBuffer>> clientBuffers;
    auto registerBuffer = [&buffers, &clientBuffers, &pool] {
        std::shared_ptr<C2GraphicBlock> block;
        ASSERT_EQ(OK, pool->fetchGraphicBlock(
                kWidth, kHeight, HAL_PIXEL_FORMAT_YCbCr_420_888,
                C2MemoryUsage{C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE}, &block));
        std::shared_ptr<C2Buffer> c2Buffer = C2Buffer::CreateGraphicBuffer(block->share(
                block->crop(), C2Fence{}));
        size_t index;
        sp<MediaCodecBuffer> clientBuffer;
        ASSERT_EQ(OK, buffers->registerBuffer(c2Buffer, &index, &clientBuffer));
        ASSERT_NE(nullptr, clientBuffer);
        while (clientBuffers.size() <= index) {
            clientBuffers.emplace_back();
        }
        ASSERT_EQ(nullptr, clientBuffers[index]) << "index = " << index;
        clientBuffers[index] = clientBuffer;
    };
    for (int i = 0; i < 4; ++i) {
        registerBuffer();
    }

    // Release 2 buffers
    auto releaseBuffer = [&buffers, &clientBuffers, kWidth, kHeight](int index) {
        std::shared_ptr<C2Buffer> c2Buffer;
        ASSERT_TRUE(buffers->releaseBuffer(clientBuffers[index], &c2Buffer))
                << "index = " << index;
        clientBuffers[index] = nullptr;
        // Sanity checks
        ASSERT_TRUE(c2Buffer->data().linearBlocks().empty());
        ASSERT_EQ(1u, c2Buffer->data().graphicBlocks().size());
        C2ConstGraphicBlock block = c2Buffer->data().graphicBlocks().front();
        ASSERT_EQ(kWidth, block.width());
        ASSERT_EQ(kHeight, block.height());
    };
    for (int i = 0, index = 0; i < 2 && index < clientBuffers.size(); ++index) {
        if (clientBuffers[index] == nullptr) {
            continue;
        }
        releaseBuffer(index);
        ++i;
    }

    // Simulate # of slots 4->16
    for (int i = 2; i < 16; ++i) {
        registerBuffer();
    }

    // Release everything
    for (int index = 0; index < clientBuffers.size(); ++index) {
        if (clientBuffers[index] == nullptr) {
            continue;
        }
        releaseBuffer(index);
    }
}

TEST(RawGraphicOutputBuffersTest, WrapNullBuffer) {
    constexpr int32_t kWidth = 320;
    constexpr int32_t kHeight = 240;

    std::shared_ptr<RawGraphicOutputBuffers> buffers =
        GetRawGraphicOutputBuffers(kWidth, kHeight);

    sp<Codec2Buffer> buffer = buffers->wrap(nullptr);
    ASSERT_EQ(nullptr, buffer->base());
    ASSERT_EQ(0, buffer->size());
    ASSERT_EQ(0, buffer->offset());
}

TEST(RawGraphicOutputBuffersTest, FlexYuvColorFormat) {
    constexpr int32_t kWidth = 320;
    constexpr int32_t kHeight = 240;

    std::vector<uint32_t> flexPixelFormats({HAL_PIXEL_FORMAT_YCbCr_420_888});
    std::shared_ptr<Codec2Client> client = Codec2Client::CreateFromService("default");
    if (client) {
        // Query vendor format for Flexible YUV
        std::vector<std::unique_ptr<C2Param>> heapParams;
        C2StoreFlexiblePixelFormatDescriptorsInfo *pixelFormatInfo = nullptr;
        if (client->query(
                    {},
                    {C2StoreFlexiblePixelFormatDescriptorsInfo::PARAM_TYPE},
                    C2_MAY_BLOCK,
                    &heapParams) == C2_OK
                && heapParams.size() == 1u) {
            pixelFormatInfo = C2StoreFlexiblePixelFormatDescriptorsInfo::From(
                    heapParams[0].get());
        } else {
            pixelFormatInfo = nullptr;
        }
        if (pixelFormatInfo && *pixelFormatInfo) {
            for (size_t i = 0; i < pixelFormatInfo->flexCount(); ++i) {
                const C2FlexiblePixelFormatDescriptorStruct &desc =
                    pixelFormatInfo->m.values[i];
                if (desc.bitDepth != 8
                        || desc.subsampling != C2Color::YUV_420
                        // TODO(b/180076105): some devices report wrong layouts
                        // || desc.layout == C2Color::INTERLEAVED_PACKED
                        // || desc.layout == C2Color::INTERLEAVED_ALIGNED
                        || desc.layout == C2Color::UNKNOWN_LAYOUT) {
                    continue;
                }
                flexPixelFormats.push_back(desc.pixelFormat);
            }
        }
    }

    for (uint32_t pixelFormat : flexPixelFormats) {
        std::shared_ptr<RawGraphicOutputBuffers> buffers =
            std::make_shared<RawGraphicOutputBuffers>(
                    AStringPrintf("test pixel format 0x%x", pixelFormat).c_str());

        sp<AMessage> format{new AMessage};
        format->setInt32(KEY_WIDTH, kWidth);
        format->setInt32(KEY_HEIGHT, kHeight);
        format->setInt32(KEY_COLOR_FORMAT, COLOR_FormatYUV420Flexible);
        int32_t fwkPixelFormat = 0;
        if (C2Mapper::mapPixelFormatCodecToFramework(pixelFormat, &fwkPixelFormat)) {
            format->setInt32("android._color-format", fwkPixelFormat);
        }
        buffers->setFormat(format);

        std::shared_ptr<C2BlockPool> pool;
        ASSERT_EQ(OK, GetCodec2BlockPool(C2BlockPool::BASIC_GRAPHIC, nullptr, &pool));

        std::shared_ptr<C2GraphicBlock> block;
        ASSERT_EQ(OK, pool->fetchGraphicBlock(
                kWidth, kHeight, pixelFormat,
                C2MemoryUsage{C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE}, &block));

        {
            C2GraphicView view = block->map().get();
            C2PlanarLayout layout = view.layout();

            // Verify the block is in YUV420 format
            ASSERT_EQ(C2PlanarLayout::TYPE_YUV, layout.type);
            ASSERT_EQ(3u, layout.numPlanes);
            const C2PlaneInfo& yPlane = layout.planes[C2PlanarLayout::PLANE_Y];
            const C2PlaneInfo& uPlane = layout.planes[C2PlanarLayout::PLANE_U];
            const C2PlaneInfo& vPlane = layout.planes[C2PlanarLayout::PLANE_V];

            // Y plane
            ASSERT_EQ(1u, yPlane.colSampling);
            ASSERT_EQ(1u, yPlane.rowSampling);
            ASSERT_EQ(8u, yPlane.allocatedDepth);
            ASSERT_EQ(8u, yPlane.bitDepth);
            ASSERT_EQ(0u, yPlane.rightShift);

            // U plane
            ASSERT_EQ(2u, uPlane.colSampling);
            ASSERT_EQ(2u, uPlane.rowSampling);
            ASSERT_EQ(8u, uPlane.allocatedDepth);
            ASSERT_EQ(8u, uPlane.bitDepth);
            ASSERT_EQ(0u, uPlane.rightShift);

            // V plane
            ASSERT_EQ(2u, vPlane.colSampling);
            ASSERT_EQ(2u, vPlane.rowSampling);
            ASSERT_EQ(8u, vPlane.allocatedDepth);
            ASSERT_EQ(8u, vPlane.bitDepth);
            ASSERT_EQ(0u, vPlane.rightShift);

            uint8_t *yRowPtr = view.data()[C2PlanarLayout::PLANE_Y];
            uint8_t *uRowPtr = view.data()[C2PlanarLayout::PLANE_U];
            uint8_t *vRowPtr = view.data()[C2PlanarLayout::PLANE_V];
            for (int32_t row = 0; row < kHeight; ++row) {
                uint8_t *yPtr = yRowPtr;
                uint8_t *uPtr = uRowPtr;
                uint8_t *vPtr = vRowPtr;
                for (int32_t col = 0; col < kWidth; ++col) {
                    *yPtr = ((row + col) & 0xFF);
                    yPtr += yPlane.colInc;

                    if (row < kHeight / 2 && col < kWidth / 2) {
                        *uPtr = ((row + col + 1) & 0xFF);
                        *vPtr = ((row + col + 2) & 0xFF);
                        uPtr += uPlane.colInc;
                        vPtr += vPlane.colInc;
                    }
                }
                yRowPtr += yPlane.rowInc;
                if (row < kHeight / 2) {
                    uRowPtr += uPlane.rowInc;
                    vRowPtr += vPlane.rowInc;
                }
            }
        }

        std::shared_ptr<C2Buffer> c2Buffer = C2Buffer::CreateGraphicBuffer(block->share(
                block->crop(), C2Fence{}));
        size_t index;
        sp<MediaCodecBuffer> clientBuffer;
        ASSERT_EQ(OK, buffers->registerBuffer(c2Buffer, &index, &clientBuffer));
        ASSERT_NE(nullptr, clientBuffer);
        sp<ABuffer> imageData;
        ASSERT_TRUE(clientBuffer->format()->findBuffer("image-data", &imageData));
        MediaImage2 *img = (MediaImage2 *)imageData->data();
        ASSERT_EQ(MediaImage2::MEDIA_IMAGE_TYPE_YUV, img->mType);
        ASSERT_EQ(3u, img->mNumPlanes);
        ASSERT_EQ(kWidth, img->mWidth);
        ASSERT_EQ(kHeight, img->mHeight);
        ASSERT_EQ(8u, img->mBitDepth);
        ASSERT_EQ(8u, img->mBitDepthAllocated);
        const MediaImage2::PlaneInfo &yPlane = img->mPlane[MediaImage2::Y];
        const MediaImage2::PlaneInfo &uPlane = img->mPlane[MediaImage2::U];
        const MediaImage2::PlaneInfo &vPlane = img->mPlane[MediaImage2::V];
        ASSERT_EQ(1u, yPlane.mHorizSubsampling);
        ASSERT_EQ(1u, yPlane.mVertSubsampling);
        ASSERT_EQ(2u, uPlane.mHorizSubsampling);
        ASSERT_EQ(2u, uPlane.mVertSubsampling);
        ASSERT_EQ(2u, vPlane.mHorizSubsampling);
        ASSERT_EQ(2u, vPlane.mVertSubsampling);

        uint8_t *yRowPtr = clientBuffer->data() + yPlane.mOffset;
        uint8_t *uRowPtr = clientBuffer->data() + uPlane.mOffset;
        uint8_t *vRowPtr = clientBuffer->data() + vPlane.mOffset;
        for (int32_t row = 0; row < kHeight; ++row) {
            uint8_t *yPtr = yRowPtr;
            uint8_t *uPtr = uRowPtr;
            uint8_t *vPtr = vRowPtr;
            for (int32_t col = 0; col < kWidth; ++col) {
                ASSERT_EQ((row + col) & 0xFF, *yPtr);
                yPtr += yPlane.mColInc;
                if (row < kHeight / 2 && col < kWidth / 2) {
                    ASSERT_EQ((row + col + 1) & 0xFF, *uPtr);
                    ASSERT_EQ((row + col + 2) & 0xFF, *vPtr);
                    uPtr += uPlane.mColInc;
                    vPtr += vPlane.mColInc;
                }
            }
            yRowPtr += yPlane.mRowInc;
            if (row < kHeight / 2) {
                uRowPtr += uPlane.mRowInc;
                vRowPtr += vPlane.mRowInc;
            }
        }
    }
}

TEST(RawGraphicOutputBuffersTest, P010ColorFormat) {
    constexpr int32_t kWidth = 320;
    constexpr int32_t kHeight = 240;

    std::shared_ptr<RawGraphicOutputBuffers> buffers =
        std::make_shared<RawGraphicOutputBuffers>("test P010");

    sp<AMessage> format{new AMessage};
    format->setInt32(KEY_WIDTH, kWidth);
    format->setInt32(KEY_HEIGHT, kHeight);
    format->setInt32(KEY_COLOR_FORMAT, COLOR_FormatYUVP010);
    int32_t fwkPixelFormat = 0;
    if (C2Mapper::mapPixelFormatCodecToFramework(HAL_PIXEL_FORMAT_YCBCR_P010, &fwkPixelFormat)) {
        format->setInt32("android._color-format", fwkPixelFormat);
    }
    buffers->setFormat(format);

    std::shared_ptr<C2BlockPool> pool;
    ASSERT_EQ(OK, GetCodec2BlockPool(C2BlockPool::BASIC_GRAPHIC, nullptr, &pool));

    std::shared_ptr<C2GraphicBlock> block;
    c2_status_t err = pool->fetchGraphicBlock(
            kWidth, kHeight, HAL_PIXEL_FORMAT_YCBCR_P010,
            C2MemoryUsage{C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE}, &block);
    if (err != C2_OK) {
        GTEST_SKIP();
    }

    {
        C2GraphicView view = block->map().get();
        C2PlanarLayout layout = view.layout();

        // Verify the block is in YUV420 format
        ASSERT_EQ(C2PlanarLayout::TYPE_YUV, layout.type);
        ASSERT_EQ(3u, layout.numPlanes);
        const C2PlaneInfo& yPlane = layout.planes[C2PlanarLayout::PLANE_Y];
        const C2PlaneInfo& uPlane = layout.planes[C2PlanarLayout::PLANE_U];
        const C2PlaneInfo& vPlane = layout.planes[C2PlanarLayout::PLANE_V];

        // Y plane
        ASSERT_EQ(1u, yPlane.colSampling);
        ASSERT_EQ(1u, yPlane.rowSampling);
        ASSERT_EQ(16u, yPlane.allocatedDepth);
        ASSERT_EQ(10u, yPlane.bitDepth);
        ASSERT_EQ(6u, yPlane.rightShift);

        // U plane
        ASSERT_EQ(2u, uPlane.colSampling);
        ASSERT_EQ(2u, uPlane.rowSampling);
        ASSERT_EQ(16u, uPlane.allocatedDepth);
        ASSERT_EQ(10u, uPlane.bitDepth);
        ASSERT_EQ(6u, uPlane.rightShift);

        // V plane
        ASSERT_EQ(2u, vPlane.colSampling);
        ASSERT_EQ(2u, vPlane.rowSampling);
        ASSERT_EQ(16u, vPlane.allocatedDepth);
        ASSERT_EQ(10u, vPlane.bitDepth);
        ASSERT_EQ(6u, vPlane.rightShift);

        uint8_t *yRowPtr = view.data()[C2PlanarLayout::PLANE_Y];
        uint8_t *uRowPtr = view.data()[C2PlanarLayout::PLANE_U];
        uint8_t *vRowPtr = view.data()[C2PlanarLayout::PLANE_V];
        for (int32_t row = 0; row < kHeight; ++row) {
            uint8_t *yPtr = yRowPtr;
            uint8_t *uPtr = uRowPtr;
            uint8_t *vPtr = vRowPtr;
            for (int32_t col = 0; col < kWidth; ++col) {
                yPtr[0] = ((row + col) & 0x3) << 6;
                yPtr[1] = ((row + col) & 0x3FC) >> 2;
                yPtr += yPlane.colInc;

                if (row < kHeight / 2 && col < kWidth / 2) {
                    uPtr[0] = ((row + col + 1) & 0x3) << 6;
                    uPtr[1] = ((row + col + 1) & 0x3FC) >> 2;
                    vPtr[0] = ((row + col + 2) & 0x3) << 6;
                    vPtr[1] = ((row + col + 2) & 0x3FC) >> 2;
                    uPtr += uPlane.colInc;
                    vPtr += vPlane.colInc;
                }
            }
            yRowPtr += yPlane.rowInc;
            if (row < kHeight / 2) {
                uRowPtr += uPlane.rowInc;
                vRowPtr += vPlane.rowInc;
            }
        }
    }

    std::shared_ptr<C2Buffer> c2Buffer = C2Buffer::CreateGraphicBuffer(block->share(
            block->crop(), C2Fence{}));
    size_t index;
    sp<MediaCodecBuffer> clientBuffer;
    ASSERT_EQ(OK, buffers->registerBuffer(c2Buffer, &index, &clientBuffer));
    ASSERT_NE(nullptr, clientBuffer);
    sp<ABuffer> imageData;
    ASSERT_TRUE(clientBuffer->format()->findBuffer("image-data", &imageData));
    MediaImage2 *img = (MediaImage2 *)imageData->data();
    ASSERT_EQ(MediaImage2::MEDIA_IMAGE_TYPE_YUV, img->mType);
    ASSERT_EQ(3u, img->mNumPlanes);
    ASSERT_EQ(kWidth, img->mWidth);
    ASSERT_EQ(kHeight, img->mHeight);
    ASSERT_EQ(10u, img->mBitDepth);
    ASSERT_EQ(16u, img->mBitDepthAllocated);
    const MediaImage2::PlaneInfo &yPlane = img->mPlane[MediaImage2::Y];
    const MediaImage2::PlaneInfo &uPlane = img->mPlane[MediaImage2::U];
    const MediaImage2::PlaneInfo &vPlane = img->mPlane[MediaImage2::V];
    ASSERT_EQ(1u, yPlane.mHorizSubsampling);
    ASSERT_EQ(1u, yPlane.mVertSubsampling);
    ASSERT_EQ(2u, uPlane.mHorizSubsampling);
    ASSERT_EQ(2u, uPlane.mVertSubsampling);
    ASSERT_EQ(2u, vPlane.mHorizSubsampling);
    ASSERT_EQ(2u, vPlane.mVertSubsampling);

    uint8_t *yRowPtr = clientBuffer->data() + yPlane.mOffset;
    uint8_t *uRowPtr = clientBuffer->data() + uPlane.mOffset;
    uint8_t *vRowPtr = clientBuffer->data() + vPlane.mOffset;
    for (int32_t row = 0; row < kHeight; ++row) {
        uint8_t *yPtr = yRowPtr;
        uint8_t *uPtr = uRowPtr;
        uint8_t *vPtr = vRowPtr;
        for (int32_t col = 0; col < kWidth; ++col) {
            ASSERT_EQ(((row + col) & 0x3) << 6, yPtr[0]);
            ASSERT_EQ(((row + col) & 0x3FC) >> 2, yPtr[1]);
            yPtr += yPlane.mColInc;
            if (row < kHeight / 2 && col < kWidth / 2) {
                ASSERT_EQ(((row + col + 1) & 0x3) << 6, uPtr[0]);
                ASSERT_EQ(((row + col + 1) & 0x3FC) >> 2, uPtr[1]);
                ASSERT_EQ(((row + col + 2) & 0x3) << 6, vPtr[0]);
                ASSERT_EQ(((row + col + 2) & 0x3FC) >> 2, vPtr[1]);
                uPtr += uPlane.mColInc;
                vPtr += vPlane.mColInc;
            }
        }
        yRowPtr += yPlane.mRowInc;
        if (row < kHeight / 2) {
            uRowPtr += uPlane.mRowInc;
            vRowPtr += vPlane.mRowInc;
        }
    }
}

class TestGraphicAllocation : public C2GraphicAllocation {
public:
    TestGraphicAllocation(
            uint32_t width,
            uint32_t height,
            const C2PlanarLayout &layout,
            size_t capacity,
            std::vector<size_t> offsets)
        : C2GraphicAllocation(width, height),
          mLayout(layout),
          mMemory(capacity, 0xAA),
          mOffsets(offsets) {
    }

    c2_status_t map(
            C2Rect rect, C2MemoryUsage usage, C2Fence *fence,
            C2PlanarLayout *layout, uint8_t **addr) override {
        (void)rect;
        (void)usage;
        (void)fence;
        *layout = mLayout;
        for (size_t i = 0; i < mLayout.numPlanes; ++i) {
            addr[i] = mMemory.data() + mOffsets[i];
        }
        return C2_OK;
    }

    c2_status_t unmap(uint8_t **, C2Rect, C2Fence *) override { return C2_OK; }

    C2Allocator::id_t getAllocatorId() const override { return -1; }

    const C2Handle *handle() const override { return nullptr; }

    bool equals(const std::shared_ptr<const C2GraphicAllocation> &other) const override {
        return other.get() == this;
    }

private:
    C2PlanarLayout mLayout;
    std::vector<uint8_t> mMemory;
    std::vector<uint8_t *> mAddr;
    std::vector<size_t> mOffsets;
};

class LayoutTest : public ::testing::TestWithParam<std::tuple<bool, std::string, bool, int32_t>> {
private:
    static C2PlanarLayout YUVPlanarLayout(int32_t stride) {
        C2PlanarLayout layout = {
            C2PlanarLayout::TYPE_YUV,
            3,  /* numPlanes */
            3,  /* rootPlanes */
            {},  /* planes --- to be filled below */
        };
        layout.planes[C2PlanarLayout::PLANE_Y] = {
            C2PlaneInfo::CHANNEL_Y,
            1,  /* colInc */
            stride,  /* rowInc */
            1,  /* colSampling */
            1,  /* rowSampling */
            8,  /* allocatedDepth */
            8,  /* bitDepth */
            0,  /* rightShift */
            C2PlaneInfo::NATIVE,
            C2PlanarLayout::PLANE_Y,  /* rootIx */
            0,  /* offset */
        };
        layout.planes[C2PlanarLayout::PLANE_U] = {
            C2PlaneInfo::CHANNEL_CB,
            1,  /* colInc */
            stride / 2,  /* rowInc */
            2,  /* colSampling */
            2,  /* rowSampling */
            8,  /* allocatedDepth */
            8,  /* bitDepth */
            0,  /* rightShift */
            C2PlaneInfo::NATIVE,
            C2PlanarLayout::PLANE_U,  /* rootIx */
            0,  /* offset */
        };
        layout.planes[C2PlanarLayout::PLANE_V] = {
            C2PlaneInfo::CHANNEL_CR,
            1,  /* colInc */
            stride / 2,  /* rowInc */
            2,  /* colSampling */
            2,  /* rowSampling */
            8,  /* allocatedDepth */
            8,  /* bitDepth */
            0,  /* rightShift */
            C2PlaneInfo::NATIVE,
            C2PlanarLayout::PLANE_V,  /* rootIx */
            0,  /* offset */
        };
        return layout;
    }

    static C2PlanarLayout YUVSemiPlanarLayout(int32_t stride) {
        C2PlanarLayout layout = {
            C2PlanarLayout::TYPE_YUV,
            3,  /* numPlanes */
            2,  /* rootPlanes */
            {},  /* planes --- to be filled below */
        };
        layout.planes[C2PlanarLayout::PLANE_Y] = {
            C2PlaneInfo::CHANNEL_Y,
            1,  /* colInc */
            stride,  /* rowInc */
            1,  /* colSampling */
            1,  /* rowSampling */
            8,  /* allocatedDepth */
            8,  /* bitDepth */
            0,  /* rightShift */
            C2PlaneInfo::NATIVE,
            C2PlanarLayout::PLANE_Y,  /* rootIx */
            0,  /* offset */
        };
        layout.planes[C2PlanarLayout::PLANE_U] = {
            C2PlaneInfo::CHANNEL_CB,
            2,  /* colInc */
            stride,  /* rowInc */
            2,  /* colSampling */
            2,  /* rowSampling */
            8,  /* allocatedDepth */
            8,  /* bitDepth */
            0,  /* rightShift */
            C2PlaneInfo::NATIVE,
            C2PlanarLayout::PLANE_U,  /* rootIx */
            0,  /* offset */
        };
        layout.planes[C2PlanarLayout::PLANE_V] = {
            C2PlaneInfo::CHANNEL_CR,
            2,  /* colInc */
            stride,  /* rowInc */
            2,  /* colSampling */
            2,  /* rowSampling */
            8,  /* allocatedDepth */
            8,  /* bitDepth */
            0,  /* rightShift */
            C2PlaneInfo::NATIVE,
            C2PlanarLayout::PLANE_U,  /* rootIx */
            1,  /* offset */
        };
        return layout;
    }

    static C2PlanarLayout YVUSemiPlanarLayout(int32_t stride) {
        C2PlanarLayout layout = {
            C2PlanarLayout::TYPE_YUV,
            3,  /* numPlanes */
            2,  /* rootPlanes */
            {},  /* planes --- to be filled below */
        };
        layout.planes[C2PlanarLayout::PLANE_Y] = {
            C2PlaneInfo::CHANNEL_Y,
            1,  /* colInc */
            stride,  /* rowInc */
            1,  /* colSampling */
            1,  /* rowSampling */
            8,  /* allocatedDepth */
            8,  /* bitDepth */
            0,  /* rightShift */
            C2PlaneInfo::NATIVE,
            C2PlanarLayout::PLANE_Y,  /* rootIx */
            0,  /* offset */
        };
        layout.planes[C2PlanarLayout::PLANE_U] = {
            C2PlaneInfo::CHANNEL_CB,
            2,  /* colInc */
            stride,  /* rowInc */
            2,  /* colSampling */
            2,  /* rowSampling */
            8,  /* allocatedDepth */
            8,  /* bitDepth */
            0,  /* rightShift */
            C2PlaneInfo::NATIVE,
            C2PlanarLayout::PLANE_V,  /* rootIx */
            1,  /* offset */
        };
        layout.planes[C2PlanarLayout::PLANE_V] = {
            C2PlaneInfo::CHANNEL_CR,
            2,  /* colInc */
            stride,  /* rowInc */
            2,  /* colSampling */
            2,  /* rowSampling */
            8,  /* allocatedDepth */
            8,  /* bitDepth */
            0,  /* rightShift */
            C2PlaneInfo::NATIVE,
            C2PlanarLayout::PLANE_V,  /* rootIx */
            0,  /* offset */
        };
        return layout;
    }

    static std::shared_ptr<C2GraphicBlock> CreateGraphicBlock(
            uint32_t width,
            uint32_t height,
            const C2PlanarLayout &layout,
            size_t capacity,
            std::vector<size_t> offsets) {
        std::shared_ptr<C2GraphicAllocation> alloc = std::make_shared<TestGraphicAllocation>(
                width,
                height,
                layout,
                capacity,
                offsets);

        return _C2BlockFactory::CreateGraphicBlock(alloc);
    }

    static constexpr uint8_t GetPixelValue(uint8_t value, uint32_t row, uint32_t col) {
        return (uint32_t(value) * row + col) & 0xFF;
    }

    static void FillPlane(C2GraphicView &view, size_t index, uint8_t value) {
        C2PlanarLayout layout = view.layout();

        uint8_t *rowPtr = view.data()[index];
        C2PlaneInfo plane = layout.planes[index];
        for (uint32_t row = 0; row < view.height() / plane.rowSampling; ++row) {
            uint8_t *colPtr = rowPtr;
            for (uint32_t col = 0; col < view.width() / plane.colSampling; ++col) {
                *colPtr = GetPixelValue(value, row, col);
                colPtr += plane.colInc;
            }
            rowPtr += plane.rowInc;
        }
    }

    static void FillBlock(const std::shared_ptr<C2GraphicBlock> &block) {
        C2GraphicView view = block->map().get();

        FillPlane(view, C2PlanarLayout::PLANE_Y, 'Y');
        FillPlane(view, C2PlanarLayout::PLANE_U, 'U');
        FillPlane(view, C2PlanarLayout::PLANE_V, 'V');
    }

    static bool VerifyPlane(
            const MediaImage2 *mediaImage,
            const uint8_t *base,
            uint32_t index,
            uint8_t value,
            std::string *errorMsg) {
        *errorMsg = "";
        MediaImage2::PlaneInfo plane = mediaImage->mPlane[index];
        const uint8_t *rowPtr = base + plane.mOffset;
        for (uint32_t row = 0; row < mediaImage->mHeight / plane.mVertSubsampling; ++row) {
            const uint8_t *colPtr = rowPtr;
            for (uint32_t col = 0; col < mediaImage->mWidth / plane.mHorizSubsampling; ++col) {
                if (GetPixelValue(value, row, col) != *colPtr) {
                    *errorMsg = AStringPrintf("row=%u col=%u expected=%02x actual=%02x",
                            row, col, GetPixelValue(value, row, col), *colPtr).c_str();
                    return false;
                }
                colPtr += plane.mColInc;
            }
            rowPtr += plane.mRowInc;
        }
        return true;
    }

public:
    static constexpr int32_t kWidth = 320;
    static constexpr int32_t kHeight = 240;
    static constexpr int32_t kGapLength = kWidth * kHeight * 10;

    static std::shared_ptr<C2Buffer> CreateAndFillBufferFromParam(const ParamType &param) {
        bool contiguous = std::get<0>(param);
        std::string planeOrderStr = std::get<1>(param);
        bool planar = std::get<2>(param);
        int32_t stride = std::get<3>(param);

        C2PlanarLayout::plane_index_t planeOrder[3];
        C2PlanarLayout layout;

        if (planeOrderStr.size() != 3) {
            return nullptr;
        }
        for (size_t i = 0; i < 3; ++i) {
            C2PlanarLayout::plane_index_t planeIndex;
            switch (planeOrderStr[i]) {
                case 'Y': planeIndex = C2PlanarLayout::PLANE_Y; break;
                case 'U': planeIndex = C2PlanarLayout::PLANE_U; break;
                case 'V': planeIndex = C2PlanarLayout::PLANE_V; break;
                default:  return nullptr;
            }
            planeOrder[i] = planeIndex;
        }

        if (planar) {
            layout = YUVPlanarLayout(stride);
        } else {  // semi-planar
            for (size_t i = 0; i < 3; ++i) {
                if (planeOrder[i] == C2PlanarLayout::PLANE_U) {
                    layout = YUVSemiPlanarLayout(stride);
                    break;
                }
                if (planeOrder[i] == C2PlanarLayout::PLANE_V) {
                    layout = YVUSemiPlanarLayout(stride);
                    break;
                }
            }
        }
        size_t yPlaneSize = stride * kHeight;
        size_t uvPlaneSize = stride * kHeight / 4;
        size_t capacity = yPlaneSize + uvPlaneSize * 2;
        std::vector<size_t> offsets(3);

        if (!contiguous) {
            if (planar) {
                capacity += kGapLength * 2;
            } else {  // semi-planar
                capacity += kGapLength;
            }
        }

        offsets[planeOrder[0]] = 0;
        size_t planeSize = (planeOrder[0] == C2PlanarLayout::PLANE_Y) ? yPlaneSize : uvPlaneSize;
        for (size_t i = 1; i < 3; ++i) {
            offsets[planeOrder[i]] = offsets[planeOrder[i - 1]] + planeSize;
            if (!contiguous) {
                offsets[planeOrder[i]] += kGapLength;
            }
            planeSize = (planeOrder[i] == C2PlanarLayout::PLANE_Y) ? yPlaneSize : uvPlaneSize;
            if (!planar  // semi-planar
                    && planeOrder[i - 1] != C2PlanarLayout::PLANE_Y
                    && planeOrder[i] != C2PlanarLayout::PLANE_Y) {
                offsets[planeOrder[i]] = offsets[planeOrder[i - 1]] + 1;
                planeSize = uvPlaneSize * 2 - 1;
            }
        }

        std::shared_ptr<C2GraphicBlock> block = CreateGraphicBlock(
                kWidth,
                kHeight,
                layout,
                capacity,
                offsets);
        FillBlock(block);
        return C2Buffer::CreateGraphicBuffer(
                block->share(block->crop(), C2Fence()));
    }

    static bool VerifyClientBuffer(
            const sp<MediaCodecBuffer> &buffer, std::string *errorMsg) {
        *errorMsg = "";
        sp<ABuffer> imageData;
        if (!buffer->format()->findBuffer("image-data", &imageData)) {
            *errorMsg = "Missing image data";
            return false;
        }
        MediaImage2 *mediaImage = (MediaImage2 *)imageData->data();
        if (mediaImage->mType != MediaImage2::MEDIA_IMAGE_TYPE_YUV) {
            *errorMsg = AStringPrintf("Unexpected type: %d", mediaImage->mType).c_str();
            return false;
        }
        std::string planeErrorMsg;
        if (!VerifyPlane(mediaImage, buffer->base(), MediaImage2::Y, 'Y', &planeErrorMsg)) {
            *errorMsg = "Y plane does not match: " + planeErrorMsg;
            return false;
        }
        if (!VerifyPlane(mediaImage, buffer->base(), MediaImage2::U, 'U', &planeErrorMsg)) {
            *errorMsg = "U plane does not match: " + planeErrorMsg;
            return false;
        }
        if (!VerifyPlane(mediaImage, buffer->base(), MediaImage2::V, 'V', &planeErrorMsg)) {
            *errorMsg = "V plane does not match: " + planeErrorMsg;
            return false;
        }

        int32_t width, height, stride;
        buffer->format()->findInt32(KEY_WIDTH, &width);
        buffer->format()->findInt32(KEY_HEIGHT, &height);
        buffer->format()->findInt32(KEY_STRIDE, &stride);

        MediaImage2 legacyYLayout = {
            MediaImage2::MEDIA_IMAGE_TYPE_Y,
            1,  // mNumPlanes
            uint32_t(width),
            uint32_t(height),
            8,
            8,
            {},  // mPlane
        };
        legacyYLayout.mPlane[MediaImage2::Y] = {
            0,  // mOffset
            1,  // mColInc
            stride,  // mRowInc
            1,  // mHorizSubsampling
            1,  // mVertSubsampling
        };
        if (!VerifyPlane(&legacyYLayout, buffer->data(), MediaImage2::Y, 'Y', &planeErrorMsg)) {
            *errorMsg = "Y plane by legacy layout does not match: " + planeErrorMsg;
            return false;
        }
        return true;
    }

};

TEST_P(LayoutTest, VerifyLayout) {
    std::shared_ptr<RawGraphicOutputBuffers> buffers =
        GetRawGraphicOutputBuffers(kWidth, kHeight);

    std::shared_ptr<C2Buffer> c2Buffer = CreateAndFillBufferFromParam(GetParam());
    ASSERT_NE(nullptr, c2Buffer);
    sp<MediaCodecBuffer> clientBuffer;
    size_t index;
    ASSERT_EQ(OK, buffers->registerBuffer(c2Buffer, &index, &clientBuffer));
    ASSERT_NE(nullptr, clientBuffer);
    std::string errorMsg;
    ASSERT_TRUE(VerifyClientBuffer(clientBuffer, &errorMsg)) << errorMsg;
}

INSTANTIATE_TEST_SUITE_P(
        RawGraphicOutputBuffersTest,
        LayoutTest,
        ::testing::Combine(
            ::testing::Bool(),  /* contiguous */
            ::testing::Values("YUV", "YVU", "UVY", "VUY"),
            ::testing::Bool(),  /* planar */
            ::testing::Values(320, 512)),
        [](const ::testing::TestParamInfo<LayoutTest::ParamType> &info) {
            std::string contiguous = std::get<0>(info.param) ? "Contiguous" : "Noncontiguous";
            std::string planar = std::get<2>(info.param) ? "Planar" : "SemiPlanar";
            return contiguous
                    + std::get<1>(info.param)
                    + planar
                    + std::to_string(std::get<3>(info.param));
        });

namespace {

// Describes a YUV 4:2:0 graphic block laid out in one contiguous allocation. All offsets are in
// bytes from the start of the Y plane.
struct Yuv420Spec {
    int32_t colorFormat;  // client color format
    bool planar;          // separate U/V planes; otherwise U/V are interleaved
    bool p010;            // 16-bit samples (only with !planar)
    int32_t rowInc;       // Y row increment in bytes (the allocation's stride)
    size_t uOffset;
    size_t vOffset;
};

constexpr uint32_t kWrapTestWidth = 320;
constexpr uint32_t kWrapTestHeight = 240;

C2PlanarLayout MakeYuv420Layout(const Yuv420Spec &spec) {
    const uint32_t depth = spec.p010 ? 16 : 8;
    const uint32_t bitDepth = spec.p010 ? 10 : 8;
    const uint32_t shift = spec.p010 ? 6 : 0;
    const int32_t sampleSize = spec.p010 ? 2 : 1;  // bytes
    const uint32_t vSampleOffset = spec.p010 ? 2 : 1;  // V follows U in interleaved layouts
    C2PlanarLayout layout = {
        C2PlanarLayout::TYPE_YUV,
        3,  /* numPlanes */
        spec.planar ? 3u : 2u,  /* rootPlanes */
        {},  /* planes --- to be filled below */
    };
    layout.planes[C2PlanarLayout::PLANE_Y] = {
        C2PlaneInfo::CHANNEL_Y, sampleSize, spec.rowInc, 1, 1, depth, bitDepth, shift,
        C2PlaneInfo::NATIVE, C2PlanarLayout::PLANE_Y, 0,
    };
    if (spec.planar) {
        layout.planes[C2PlanarLayout::PLANE_U] = {
            C2PlaneInfo::CHANNEL_CB, 1, spec.rowInc / 2, 2, 2, depth, bitDepth, shift,
            C2PlaneInfo::NATIVE, C2PlanarLayout::PLANE_U, 0,
        };
        layout.planes[C2PlanarLayout::PLANE_V] = {
            C2PlaneInfo::CHANNEL_CR, 1, spec.rowInc / 2, 2, 2, depth, bitDepth, shift,
            C2PlaneInfo::NATIVE, C2PlanarLayout::PLANE_V, 0,
        };
    } else {
        layout.planes[C2PlanarLayout::PLANE_U] = {
            C2PlaneInfo::CHANNEL_CB, 2 * sampleSize, spec.rowInc, 2, 2, depth, bitDepth, shift,
            C2PlaneInfo::NATIVE, C2PlanarLayout::PLANE_U, 0,
        };
        layout.planes[C2PlanarLayout::PLANE_V] = {
            C2PlaneInfo::CHANNEL_CR, 2 * sampleSize, spec.rowInc, 2, 2, depth, bitDepth, shift,
            C2PlaneInfo::NATIVE, C2PlanarLayout::PLANE_U,  /* rootIx */
            vSampleOffset,  /* offset */
        };
    }
    return layout;
}

uint8_t WrapTestPixel(uint8_t value, uint32_t row, uint32_t col) {
    return (uint32_t(value) * row + col) & 0xFF;
}

void FillWrapTestPlane(C2GraphicView &view, size_t index, uint8_t value) {
    const C2PlaneInfo &plane = view.layout().planes[index];
    uint8_t *rowPtr = view.data()[index];
    for (uint32_t row = 0; row < view.height() / plane.rowSampling; ++row) {
        uint8_t *colPtr = rowPtr;
        for (uint32_t col = 0; col < view.width() / plane.colSampling; ++col) {
            *colPtr = WrapTestPixel(value, row, col);
            colPtr += plane.colInc;
        }
        rowPtr += plane.rowInc;
    }
}

// Checks one plane of |image| (as laid out in |base|) against the pattern from FillWrapTestPlane.
::testing::AssertionResult VerifyWrapTestPlane(
        const MediaImage2 &image, const uint8_t *base, uint32_t index, uint8_t value) {
    const MediaImage2::PlaneInfo &plane = image.mPlane[index];
    const uint8_t *rowPtr = base + plane.mOffset;
    for (uint32_t row = 0; row < image.mHeight / plane.mVertSubsampling; ++row) {
        const uint8_t *colPtr = rowPtr;
        for (uint32_t col = 0; col < image.mWidth / plane.mHorizSubsampling; ++col) {
            if (*colPtr != WrapTestPixel(value, row, col)) {
                return ::testing::AssertionFailure()
                        << "plane " << index << " row=" << row << " col=" << col
                        << " expected=" << int(WrapTestPixel(value, row, col))
                        << " actual=" << int(*colPtr);
            }
            colPtr += plane.mColInc;
        }
        rowPtr += plane.mRowInc;
    }
    return ::testing::AssertionSuccess();
}

}  // namespace

// Tests which of the layouts that gralloc can produce GraphicView2MediaImageConverter may expose
// to the client as-is (wrap) and which it has to copy into the packed layout described by
// KEY_STRIDE / KEY_SLICE_HEIGHT.
class GraphicView2MediaImageConverterWrapTest : public ::testing::Test {
protected:
    void SetUp() override {
        mFormat = new AMessage;
    }

    // Builds the converter for a 320x240 block described by |spec|.
    void CreateConverter(const Yuv420Spec &spec) {
        const C2PlanarLayout layout = MakeYuv420Layout(spec);
        const size_t capacity = spec.vOffset + spec.rowInc * kWrapTestHeight;
        std::vector<size_t> offsets = {0, spec.uOffset, spec.vOffset};
        std::shared_ptr<C2GraphicAllocation> alloc = std::make_shared<TestGraphicAllocation>(
                kWrapTestWidth, kWrapTestHeight, layout, capacity, offsets);
        mBlock = _C2BlockFactory::CreateGraphicBlock(alloc);
        ASSERT_NE(nullptr, mBlock);
        C2GraphicView view = mBlock->map().get();
        ASSERT_EQ(C2_OK, view.error());
        FillWrapTestPlane(view, C2PlanarLayout::PLANE_Y, 'Y');
        FillWrapTestPlane(view, C2PlanarLayout::PLANE_U, 'U');
        FillWrapTestPlane(view, C2PlanarLayout::PLANE_V, 'V');
        mFormat->setInt32(KEY_COLOR_FORMAT, spec.colorFormat);
        mConverter = std::make_unique<GraphicView2MediaImageConverter>(
                view, mFormat, false /* copy */);
        ASSERT_EQ(OK, mConverter->initCheck());
    }

    const MediaImage2 &image() const {
        return *(const MediaImage2 *)mConverter->imageData()->data();
    }

    // Slice height a client derives from the image data, as CCodecBuffers does.
    int32_t sliceHeight() const {
        return int32_t((int64_t(image().mPlane[MediaImage2::U].mOffset)
                        - int64_t(image().mPlane[MediaImage2::Y].mOffset))
                       / image().mPlane[MediaImage2::Y].mRowInc);
    }

    // The block must not be wrapped, and copying must yield the packed 8-bit planar layout with
    // the content of every plane intact.
    void ExpectCopiedToPackedLayout() {
        ASSERT_EQ(nullptr, mConverter->wrap());
        sp<ABuffer> backBuffer = new ABuffer(mConverter->backBufferSize());
        ASSERT_TRUE(mConverter->setBackBuffer(backBuffer));
        ASSERT_EQ(OK, mConverter->copyToMediaImage());
        const MediaImage2 &img = image();
        EXPECT_EQ(int32_t(kWrapTestWidth), img.mPlane[MediaImage2::Y].mRowInc);
        EXPECT_EQ(kWrapTestWidth * kWrapTestHeight, img.mPlane[MediaImage2::U].mOffset);
        EXPECT_EQ(kWrapTestHeight, uint32_t(sliceHeight()));
        EXPECT_TRUE(VerifyWrapTestPlane(img, backBuffer->base(), MediaImage2::Y, 'Y'));
        EXPECT_TRUE(VerifyWrapTestPlane(img, backBuffer->base(), MediaImage2::U, 'U'));
        EXPECT_TRUE(VerifyWrapTestPlane(img, backBuffer->base(), MediaImage2::V, 'V'));
    }

    sp<AMessage> mFormat;
    std::shared_ptr<C2GraphicBlock> mBlock;
    std::unique_ptr<GraphicView2MediaImageConverter> mConverter;
};

// 1. Padded stride, chroma planes contiguous right after Y.
TEST_F(GraphicView2MediaImageConverterWrapTest, PlanarPaddedStrideIsWrapped) {
    const size_t u = 512 * kWrapTestHeight;
    ASSERT_NO_FATAL_FAILURE(CreateConverter(
            {COLOR_FormatYUV420Planar, true, false, 512, u, u + 256 * (kWrapTestHeight / 2)}));
    ASSERT_NE(nullptr, mConverter->wrap());
    EXPECT_EQ(512, image().mPlane[MediaImage2::Y].mRowInc);
    EXPECT_EQ(int32_t(kWrapTestHeight), sliceHeight());
}

// 2. Padded Y slice height: U starts at stride * 256 and V follows (stride / 2) * 128 later.
TEST_F(GraphicView2MediaImageConverterWrapTest, PlanarPaddedSliceHeightIsWrapped) {
    const size_t u = 320 * 256;
    ASSERT_NO_FATAL_FAILURE(CreateConverter(
            {COLOR_FormatYUV420Planar, true, false, 320, u, u + 160 * 128}));
    ASSERT_NE(nullptr, mConverter->wrap());
    EXPECT_EQ(256, sliceHeight());
}

// 3. V is padded beyond (stride / 2) * (sliceHeight / 2): the chroma plane height is padded to 128
// rows. KEY_STRIDE / KEY_SLICE_HEIGHT would put V 8 chroma rows early, so it has to be copied.
TEST_F(GraphicView2MediaImageConverterWrapTest, PlanarPaddedChromaHeightIsCopied) {
    const size_t u = 512 * kWrapTestHeight;
    ASSERT_NO_FATAL_FAILURE(CreateConverter(
            {COLOR_FormatYUV420Planar, true, false, 512, u, u + 256 * 128}));
    ExpectCopiedToPackedLayout();
}

// 4. U does not start on a row boundary of Y.
TEST_F(GraphicView2MediaImageConverterWrapTest, PlanarUOffsetNotMultipleOfStrideIsCopied) {
    const size_t u = 320 * kWrapTestHeight + 16;
    ASSERT_NO_FATAL_FAILURE(CreateConverter(
            {COLOR_FormatYUV420Planar, true, false, 320, u, u + 160 * (kWrapTestHeight / 2)}));
    ASSERT_EQ(nullptr, mConverter->wrap());
}

// Clients derive V from stride * sliceHeight / 4 or (stride / 2) * (sliceHeight / 2), which only
// agree for an even slice height.
TEST_F(GraphicView2MediaImageConverterWrapTest, PlanarOddSliceHeightIsCopied) {
    const size_t u = 320 * (kWrapTestHeight + 1);
    ASSERT_NO_FATAL_FAILURE(CreateConverter(
            {COLOR_FormatYUV420Planar, true, false, 320, u, u + 160 * (kWrapTestHeight / 2)}));
    ASSERT_EQ(nullptr, mConverter->wrap());
}

TEST_F(GraphicView2MediaImageConverterWrapTest, SemiPlanarContiguousIsWrapped) {
    const size_t u = 512 * kWrapTestHeight;
    ASSERT_NO_FATAL_FAILURE(CreateConverter(
            {COLOR_FormatYUV420SemiPlanar, false, false, 512, u, u + 1}));
    ASSERT_NE(nullptr, mConverter->wrap());
    EXPECT_EQ(int32_t(kWrapTestHeight), sliceHeight());
}

// 5. Semi-planar with V != U + 1.
TEST_F(GraphicView2MediaImageConverterWrapTest, SemiPlanarVNotNextToUIsCopied) {
    const size_t u = 320 * kWrapTestHeight;
    ASSERT_NO_FATAL_FAILURE(CreateConverter(
            {COLOR_FormatYUV420SemiPlanar, false, false, 320, u, u + 2}));
    ASSERT_EQ(nullptr, mConverter->wrap());
}

// 6. P010 variants of 1 and of the "not described by stride / slice height" cases.
TEST_F(GraphicView2MediaImageConverterWrapTest, P010PaddedStrideIsWrapped) {
    const size_t u = 1024 * kWrapTestHeight;
    ASSERT_NO_FATAL_FAILURE(CreateConverter(
            {COLOR_FormatYUVP010, false, true, 1024, u, u + 2}));
    ASSERT_NE(nullptr, mConverter->wrap());
    EXPECT_EQ(1024, image().mPlane[MediaImage2::Y].mRowInc);
    EXPECT_EQ(int32_t(kWrapTestHeight), sliceHeight());
}

TEST_F(GraphicView2MediaImageConverterWrapTest, P010UOffsetNotMultipleOfStrideIsCopied) {
    const size_t u = 640 * kWrapTestHeight + 128;
    ASSERT_NO_FATAL_FAILURE(CreateConverter(
            {COLOR_FormatYUVP010, false, true, 640, u, u + 2}));
    ASSERT_EQ(nullptr, mConverter->wrap());
}

TEST(LinearOutputBuffersTest, PcmConvertFormat) {
    // Prepare LinearOutputBuffers
    std::shared_ptr<LinearOutputBuffers> buffers =
        std::make_shared<LinearOutputBuffers>("test");
    sp<AMessage> format{new AMessage};
    format->setInt32(KEY_CHANNEL_COUNT, 1);
    format->setInt32(KEY_SAMPLE_RATE, 8000);
    format->setInt32(KEY_PCM_ENCODING, kAudioEncodingPcmFloat);
    format->setInt32("android._config-pcm-encoding", kAudioEncodingPcm16bit);
    format->setInt32("android._codec-pcm-encoding", kAudioEncodingPcmFloat);
    buffers->setFormat(format);

    // Prepare a linear C2Buffer
    std::shared_ptr<C2BlockPool> pool;
    ASSERT_EQ(OK, GetCodec2BlockPool(C2BlockPool::BASIC_LINEAR, nullptr, &pool));

    std::shared_ptr<C2LinearBlock> block;
    ASSERT_EQ(OK, pool->fetchLinearBlock(
            1024, C2MemoryUsage{C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE}, &block));
    std::shared_ptr<C2Buffer> c2Buffer =
        C2Buffer::CreateLinearBuffer(block->share(0, 1024, C2Fence()));

    // Test regular buffer convert
    size_t index;
    sp<MediaCodecBuffer> clientBuffer;
    ASSERT_EQ(OK, buffers->registerBuffer(c2Buffer, &index, &clientBuffer));
    int32_t pcmEncoding = 0;
    ASSERT_TRUE(clientBuffer->format()->findInt32(KEY_PCM_ENCODING, &pcmEncoding));
    EXPECT_EQ(kAudioEncodingPcm16bit, pcmEncoding);
    ASSERT_TRUE(buffers->releaseBuffer(clientBuffer, &c2Buffer));

    // Test null buffer convert
    ASSERT_EQ(OK, buffers->registerBuffer(nullptr, &index, &clientBuffer));
    ASSERT_TRUE(clientBuffer->format()->findInt32(KEY_PCM_ENCODING, &pcmEncoding));
    EXPECT_EQ(kAudioEncodingPcm16bit, pcmEncoding);
    ASSERT_TRUE(buffers->releaseBuffer(clientBuffer, &c2Buffer));

    // Do the same test in the array mode
    std::shared_ptr<OutputBuffersArray> array = buffers->toArrayMode(8);

    // Test regular buffer convert
    ASSERT_EQ(OK, buffers->registerBuffer(c2Buffer, &index, &clientBuffer));
    ASSERT_TRUE(clientBuffer->format()->findInt32(KEY_PCM_ENCODING, &pcmEncoding));
    EXPECT_EQ(kAudioEncodingPcm16bit, pcmEncoding);
    ASSERT_TRUE(buffers->releaseBuffer(clientBuffer, &c2Buffer));

    // Test null buffer convert
    ASSERT_EQ(OK, buffers->registerBuffer(nullptr, &index, &clientBuffer));
    ASSERT_TRUE(clientBuffer->format()->findInt32(KEY_PCM_ENCODING, &pcmEncoding));
    EXPECT_EQ(kAudioEncodingPcm16bit, pcmEncoding);
    ASSERT_TRUE(buffers->releaseBuffer(clientBuffer, &c2Buffer));
}

} // namespace android
