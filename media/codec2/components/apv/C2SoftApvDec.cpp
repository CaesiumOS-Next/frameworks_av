/*
 * Copyright (C) 2018 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "C2SoftApvDec"
#include <log/log.h>

#include <media/stagefright/foundation/MediaDefs.h>
#include <media/stagefright/foundation/AUtils.h>

#include <C2Debug.h>
#include <C2PlatformSupport.h>
#include <Codec2Mapper.h>
#include <SimpleC2Interface.h>

#include "C2SoftApvDec.h"
#include "oapv_app_util.h"

#include <cutils/properties.h>

const char *MEDIA_MIMETYPE_VIDEO_APV = "video/apv";

#define MAX_BS_BUF  (128*1024*1024) /* byte */

#define MAX_NUM_FRMS        (1)  // supports only 1-frame output
#define FRM_IDX             (0)  // supports only 1-frame output
// check generic frame or not
#define IS_NON_AUX_FRM(frm) (((frm)->pbu_type == OAPV_PBU_TYPE_PRIMARY_FRAME) || ((frm)->pbu_type == OAPV_PBU_TYPE_NON_PRIMARY_FRAME))
// check auxiliary frame or not
#define IS_AUX_FRM(frm)     (!(IS_NON_AUX_FRM(frm)))
#define OUTPUT_CSP_NATIVE   (0)
#define OUTPUT_CSP_P210     (1)

namespace android {

namespace {

constexpr char COMPONENT_NAME[] = "c2.android.apv.decoder";
constexpr uint32_t kDefaultOutputDelay = 8;
constexpr uint32_t kMaxOutputDelay = 16;
constexpr size_t kMinInputBufferSize = 2 * 1024 * 1024;
}  // namespace

class C2SoftApvDec::IntfImpl : public SimpleInterface<void>::BaseParams {
public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper)
        : SimpleInterface<void>::BaseParams(
                helper,
                COMPONENT_NAME,
                C2Component::KIND_DECODER,
                C2Component::DOMAIN_VIDEO,
                "video/apv") {
//                MEDIA_MIMETYPE_VIDEO_APV) {
        noPrivateBuffers(); // TODO: account for our buffers here
        noInputReferences();
        noOutputReferences();
        noInputLatency();
        noTimeStretch();

        // TODO: Proper support for reorder depth.
        addParameter(
                DefineParam(mActualOutputDelay, C2_PARAMKEY_OUTPUT_DELAY)
                .withDefault(new C2PortActualDelayTuning::output(kDefaultOutputDelay))
                .withFields({C2F(mActualOutputDelay, value).inRange(0, kMaxOutputDelay)})
                .withSetter(Setter<decltype(*mActualOutputDelay)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
                .withConstValue(new C2ComponentAttributesSetting(C2Component::ATTRIB_IS_TEMPORAL))
                .build());

#if 0
        addParameter(
                DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                .withDefault(new C2StreamProfileLevelInfo::input(0u,
                        C2Config::PROFILE_APV_MAIN))
                .withFields({
                    C2F(mProfileLevel, profile).oneOf({
                            C2Config::PROFILE_APV_MAIN}),
                    C2F(mProfileLevel, level).oneOf({
                            C2Config::LEVEL_UNUSED
                    })
                })
                .withSetter(ProfileLevelSetter, mSize)
                .build());
#endif
        addParameter(
                DefineParam(mSize, C2_PARAMKEY_PICTURE_SIZE)
                .withDefault(new C2StreamPictureSizeInfo::output(0u, 320, 240))
                .withFields({
                    C2F(mSize, width).inRange(2, 4080, 2),
                    C2F(mSize, height).inRange(2, 4080, 2),
                })
                .withSetter(SizeSetter)
                .build());

        addParameter(
                DefineParam(mMaxSize, C2_PARAMKEY_MAX_PICTURE_SIZE)
                .withDefault(new C2StreamMaxPictureSizeTuning::output(0u, 320, 240))
                .withFields({
                    C2F(mSize, width).inRange(2, 4080, 2),
                    C2F(mSize, height).inRange(2, 4080, 2),
                })
                .withSetter(MaxPictureSizeSetter, mSize)
                .build());

        addParameter(
                DefineParam(mMaxInputSize, C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE)
                .withDefault(new C2StreamMaxBufferSizeInfo::input(0u, kMinInputBufferSize))
                .withFields({
                    C2F(mMaxInputSize, value).any(),
                })
                .calculatedAs(MaxInputSizeSetter, mMaxSize)
                .build());

#if 0
        // TODO: support more formats?
        addParameter(
                DefineParam(mPixelFormat, C2_PARAMKEY_PIXEL_FORMAT)
                .withConstValue(new C2StreamPixelFormatInfo::output(
                                     0u, HAL_PIXEL_FORMAT_YCBCR_422_888))
                .build());
#endif
    }

    static C2R SizeSetter(bool mayBlock, const C2P<C2StreamPictureSizeInfo::output> &oldMe,
                            C2P<C2StreamPictureSizeInfo::output> &me) {
        (void)mayBlock;
        C2R res = C2R::Ok();
        if (!me.F(me.v.width).supportsAtAll(me.v.width)) {
            res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.width)));
            me.set().width = oldMe.v.width;
        }
        if (!me.F(me.v.height).supportsAtAll(me.v.height)) {
            res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.height)));
            me.set().height = oldMe.v.height;
        }
        return res;
    }

    static C2R ProfileLevelSetter(bool mayBlock, C2P<C2StreamProfileLevelInfo::input> &me,
                                  const C2P<C2StreamPictureSizeInfo::output> &size) {
        ALOGV("%s, not implemented", __FUNCTION__);
        (void)mayBlock;
        (void)size;
        (void)me;  // TODO: validate
        return C2R::Ok();
    }

    static C2R MaxPictureSizeSetter(bool mayBlock, C2P<C2StreamMaxPictureSizeTuning::output> &me,
                                    const C2P<C2StreamPictureSizeInfo::output> &size) {
        (void)mayBlock;
        // TODO: get max width/height from the size's field helpers vs. hardcoding
        me.set().width = c2_min(c2_max(me.v.width, size.v.width), 4096u);
        me.set().height = c2_min(c2_max(me.v.height, size.v.height), 4096u);
        return C2R::Ok();
    }

    static C2R MaxInputSizeSetter(bool mayBlock, C2P<C2StreamMaxBufferSizeInfo::input> &me,
                                  const C2P<C2StreamMaxPictureSizeTuning::output> &maxSize) {
        (void)mayBlock;
        // assume compression ratio of 2
        me.set().value = c2_max((((maxSize.v.width + 15) / 16)
                * ((maxSize.v.height + 15) / 16) * 192), kMinInputBufferSize);
        return C2R::Ok();
    }

private:
    std::shared_ptr<C2StreamPictureSizeInfo::output> mSize;
    std::shared_ptr<C2StreamProfileLevelInfo::input> mProfileLevel;
    std::shared_ptr<C2StreamMaxPictureSizeTuning::output> mMaxSize;
    std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mMaxInputSize;
};

static void ivd_aligned_free(void *ctxt, void *mem) {
    (void) ctxt;
    free(mem);
}

C2SoftApvDec::C2SoftApvDec(
        const char *name,
        c2_node_id_t id,
        const std::shared_ptr<IntfImpl> &intfImpl)
    : SimpleC2Component(std::make_shared<SimpleInterface<IntfImpl>>(name, id, intfImpl)),
        mIntf(intfImpl),
        mDecHandle(nullptr),
        mOutBufferFlush(nullptr),
        mIvColorformat(IV_YUV_420P),
        mOutputDelay(kDefaultOutputDelay),
        mHeaderDecoded(false),
        mOutIndex(0u),
        mWidth(320),
        mHeight(240) {
        bs_buf = NULL;
        oapvdHandle = NULL;
        oapvmHandle = NULL;
        imgb = NULL;
        imgb_t = NULL;
        finfo = NULL;
        frm = NULL;
        imgb_w = NULL;
        imgb_o = NULL;

        outputDepth = 8;
        outputCsp = OUTPUT_CSP_NATIVE;
}

C2SoftApvDec::~C2SoftApvDec() {
    onRelease();
}

c2_status_t C2SoftApvDec::onInit() {
    ALOGV("%s", __FUNCTION__);
    status_t err = initDecoder();
    return err == OK ? C2_OK : C2_CORRUPTED;
}

c2_status_t C2SoftApvDec::onStop() {
    ALOGV("%s", __FUNCTION__);
    if (OK != resetDecoder()) return C2_CORRUPTED;
    resetPlugin();
    return C2_OK;
}

void C2SoftApvDec::onReset() {
    ALOGV("%s", __FUNCTION__);
    (void) onStop();
}

status_t C2SoftApvDec::deleteDecoder() {
    ALOGV("%s", __FUNCTION__);
    if(oapvdHandle) {
        oapvd_delete(oapvdHandle);
        oapvdHandle = NULL;
    }
    if(oapvmHandle) {
        oapvm_delete(oapvmHandle);
        oapvmHandle = NULL;
    }
    for(int i = 0; i < ofrms.num_frms; i++) {
        if(ofrms.frm[i].imgb != NULL) {
            ofrms.frm[i].imgb->release(ofrms.frm[i].imgb);
            ofrms.frm[i].imgb = NULL;
        }
    }
    if(imgb_w != NULL) {
        imgb_w->release(imgb_w);
        imgb_w = NULL;
    }
    return OK;
}

void C2SoftApvDec::onRelease() {
    ALOGV("%s", __FUNCTION__);
    (void) deleteDecoder();
    if (mOutBufferFlush) {
        ivd_aligned_free(nullptr, mOutBufferFlush);
        mOutBufferFlush = nullptr;
    }
    if (mOutBlock) {
        mOutBlock.reset();
    }
}

c2_status_t C2SoftApvDec::onFlush_sm() {
    ALOGV("%s", __FUNCTION__);
    return C2_OK;
}

status_t C2SoftApvDec::createDecoder() {
    ALOGV("%s", __FUNCTION__);
    return OK;
}

#if 0
status_t C2SoftApvDec::setParams(size_t stride, IVD_VIDEO_DECODE_MODE_T dec_mode) {
    return OK;
}

status_t C2SoftApvDec::getVersion() {
    return OK;
}
#endif

status_t C2SoftApvDec::initDecoder() {
    int ret;

    bs_buf = (unsigned char *)malloc(MAX_BS_BUF);
    if (bs_buf == NULL) {
        ALOGE("ERROR: cannot allocate bitstream buffer, size=%d\n", MAX_BS_BUF);
        return C2_NO_INIT;
    }
    memset(&cdesc, 0, sizeof(oapvd_cdesc_t));

    cdesc.threads = 1; // default
    oapvdHandle = oapvd_create(&cdesc, &ret);
    if(oapvdHandle == NULL) {
        ALOGE("ERROR: cannot create APV decoder (err=%d)\n", ret);
        return C2_NO_INIT;
    }
    // if(set_extra_config(apvdHandle, args_var)) {
    //    ALOGE("ERROR: cannot set extra configurations\n");
    //     return C2_NO_INIT;
    // }
    memset(&aui, 0, sizeof(oapv_au_info_t));
    memset(&ofrms, 0, sizeof(oapv_frms_t));

    oapvmHandle = oapvm_create(&ret);
    if(OAPV_FAILED(ret)) {
        ALOGE("oapvm create failed");
        oapvd_delete(oapvdHandle);
        oapvdHandle = NULL;
        return C2_NO_INIT;
    }

    ALOGV("oapvd init done");

    return OK;
}

status_t C2SoftApvDec::setFlushMode() {
    ALOGV("%s", __FUNCTION__);
    return OK;
}

status_t C2SoftApvDec::resetDecoder() {
    ALOGV("%s", __FUNCTION__);
    return OK;
}

void C2SoftApvDec::resetPlugin() {
    ALOGV("%s", __FUNCTION__);
    mSignalledOutputEos = false;
    if (mOutBlock) {
        mOutBlock.reset();
    }
}

void fillEmptyWork(const std::unique_ptr<C2Work> &work) {
    uint32_t flags = 0;
    if (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) {
        flags |= C2FrameData::FLAG_END_OF_STREAM;
        ALOGV("signalling eos");
    }
    work->worklets.front()->output.flags = (C2FrameData::flags_t)flags;
    work->worklets.front()->output.buffers.clear();
    work->worklets.front()->output.ordinal = work->input.ordinal;
    work->workletsProcessed = 1u;
}

void C2SoftApvDec::finishWork(uint64_t index, const std::unique_ptr<C2Work> &work,
                            const std::shared_ptr<C2GraphicBlock> &block) {
    std::shared_ptr<C2Buffer> buffer = createGraphicBuffer(block,
                                                           C2Rect(mWidth, mHeight));


// mOutBlock = nullptr;
    // {
    //     IntfImpl::Lock lock = mIntf->lock();
    //     buffer->setInfo(mIntf->getColorAspects_l());
    // }

    class FillWork {
       public:
        FillWork(uint32_t flags, C2WorkOrdinalStruct ordinal,
                 const std::shared_ptr<C2Buffer>& buffer)
            : mFlags(flags), mOrdinal(ordinal), mBuffer(buffer) {}
        ~FillWork() = default;

        void operator()(const std::unique_ptr<C2Work>& work) {
            work->worklets.front()->output.flags = (C2FrameData::flags_t)mFlags;
            work->worklets.front()->output.buffers.clear();
            work->worklets.front()->output.ordinal = mOrdinal;
            work->workletsProcessed = 1u;
            work->result = C2_OK;
            if (mBuffer) {
                work->worklets.front()->output.buffers.push_back(mBuffer);
            }
            ALOGV("timestamp = %lld, index = %lld, w/%s buffer",
                  mOrdinal.timestamp.peekll(), mOrdinal.frameIndex.peekll(),
                  mBuffer ? "" : "o");
        }

       private:
        const uint32_t mFlags;
        const C2WorkOrdinalStruct mOrdinal;
        const std::shared_ptr<C2Buffer> mBuffer;
    };

    auto fillWork = [buffer](const std::unique_ptr<C2Work> &work) {
        work->worklets.front()->output.flags = (C2FrameData::flags_t)0;
        work->worklets.front()->output.buffers.clear();
        work->worklets.front()->output.buffers.push_back(buffer);
        work->worklets.front()->output.ordinal = work->input.ordinal;
        work->workletsProcessed = 1u;
    };
    if (work && c2_cntr64_t(index) == work->input.ordinal.frameIndex) {
        bool eos = ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0);
        // TODO: Check if cloneAndSend can be avoided by tracking number of frames remaining
        if (eos) {
            if (buffer) {
                mOutIndex = index;
                C2WorkOrdinalStruct outOrdinal = work->input.ordinal;
                cloneAndSend(
                    mOutIndex, work,
                    FillWork(C2FrameData::FLAG_INCOMPLETE, outOrdinal, buffer));
                buffer.reset();
            }
        } else {
            fillWork(work);
        }
    } else {
        finish(index, fillWork);
    }
}


static void copyOutputBufferToYuvPlanarFrame(
        uint8_t *dst, const uint8_t *srcY, const uint8_t *srcU, const uint8_t *srcV,
        size_t srcYStride, size_t srcUStride, size_t srcVStride,
        size_t dstYStride, size_t dstUVStride,
        uint32_t width, uint32_t height) {
    uint8_t *dstStart = dst;

    for (size_t i = 0; i < height; ++i) {
         memcpy(dst, srcY, width);
         srcY += srcYStride;
         dst += dstYStride;
    }

    dst = dstStart + dstYStride * height;
    for (size_t i = 0; i < height / 2; ++i) {
         memcpy(dst, srcV, width / 2);
         srcV += srcVStride;
         dst += dstUVStride;
    }

    dst = dstStart + (dstYStride * height) + (dstUVStride * height / 2);
    for (size_t i = 0; i < height / 2; ++i) {
         memcpy(dst, srcU, width / 2);
         srcU += srcUStride;
         dst += dstUVStride;
    }
}

static void copyOutputBufferToYuv422SP(
        uint8_t *dst, const uint8_t *srcY, const uint8_t *srcU, const uint8_t *srcV,
        size_t srcYStride, size_t srcUStride, size_t srcVStride,
        size_t dstYStride, size_t dstUVStride,
        uint32_t width, uint32_t height) {
    uint8_t *dstStart = dst;

    for (size_t i = 0; i < height; ++i) {
        for(size_t j = 0; j < width; ++j) {
            dst[i * dstYStride + j] = (srcY[i * srcYStride + j * 2] >> 2) | (srcY[i * srcYStride + j * 2 + 1] << 6);
        }
    }

    dst = dstStart + dstYStride * height;
    for (size_t i = 0; i < height / 2; ++i) {
        for(size_t j = 0; j < width / 2; ++j) {
            dst[i * dstYStride + j * 2] = (srcV[i * srcVStride + j * 2] >> 2) | (srcV[i * srcVStride + j * 2 + 1] << 6);
            dst[i * dstYStride + j * 2 + 1] = (srcU[i * srcUStride + j * 2] >> 2) | (srcU[i * srcUStride + j * 2 + 1] << 6);
        }
    }
}

static void copyOutputBufferYUV422ToYUV420(uint8_t *dst, const uint8_t *srcY, const uint8_t *srcU, const uint8_t *srcV,
        size_t srcYStride, size_t srcUStride, size_t srcVStride,
        size_t dstYStride, size_t dstUVStride,
        uint32_t width, uint32_t height) {
    uint8_t *dstStart = dst;
    uint8_t *src = (uint8_t *)srcY;

    for (size_t i = 0; i < height; ++i) {
        for(size_t j = 0; j < width; ++j) {
            dst[i * dstYStride + j] = src[i * srcYStride + j];
        }
    }

    dst = dstStart + dstYStride * height;
    src = (uint8_t *)srcU;
    for (size_t i = 0; i < height / 2; ++i) {
        for(size_t j = 0; j < width / 2; ++j) {
            dst[i * dstUVStride + j] = src[i * srcUStride * 2 + j];
        }
    }
    
    dst = dstStart + (dstYStride * height) + (dstUVStride * height / 2);
    src = (uint8_t *)srcV;
    for (size_t i = 0; i < height / 2; ++i) {
        for(size_t j = 0; j < width / 2; ++j) {
            dst[i * dstUVStride + j] = src[i * srcVStride * 2 + j];
        }
    }
}

static void copyOutputBuffer10bitToYUV420(uint8_t *dst, const uint8_t *srcY, const uint8_t *srcU, const uint8_t *srcV,
        size_t srcYStride, size_t srcUStride, size_t srcVStride,
        size_t dstYStride, size_t dstUVStride,
        uint32_t width, uint32_t height) {
    uint8_t *dstStart = dst;
    uint16_t *src = (uint16_t *)srcY;

    for (size_t i = 0; i < height; ++i) {
        for(size_t j = 0; j < width; ++j) {
            dst[i * dstYStride + j] = (uint8_t)((src[i * srcYStride / 2 + j] >> 2) & 0xFF);
        }
    }

    dst = dstStart + dstYStride * height;
    src = (uint16_t *)srcU;
    for (size_t i = 0; i < height / 2; ++i) {
        for(size_t j = 0; j < width / 2; ++j) {
            dst[i * dstUVStride + j] = (uint8_t)((src[i * srcUStride + j] >> 2) & 0xFF);
        }
    }
    
    dst = dstStart + (dstYStride * height) + (dstUVStride * height / 2);
    src = (uint16_t *)srcV;
    for (size_t i = 0; i < height / 2; ++i) {
        for(size_t j = 0; j < width / 2; ++j) {
            dst[i * dstUVStride + j] = (uint8_t)((src[i * srcVStride + j] >> 2) & 0xFF);
        }
    }
}


static void copyOutputBufferToNV21(uint8_t *dst, const uint8_t *srcY, const uint8_t *srcU, const uint8_t *srcV,
        size_t srcYStride, size_t srcUStride, size_t srcVStride,
        size_t dstYStride, size_t dstUVStride,
        uint32_t width, uint32_t height) {
    uint8_t *dstStart = dst;
    uint16_t *src = (uint16_t *)srcY;
    uint16_t *u = (uint16_t *)srcU;
    uint16_t *v = (uint16_t *)srcV;

    for (size_t i = 0; i < height; ++i) {
        for(size_t j = 0; j < width; ++j) {
            dst[i * dstYStride + j] = (uint8_t)((src[i * srcYStride / 2 + j] >> 2) & 0xFF);
        }
    }

    dst = dstStart + dstYStride * height;
    for (size_t i = 0; i < height / 2; ++i) {
        for(size_t j = 0; j < width / 2; ++j) {
            dst[i * dstYStride + j * 2] = (uint8_t)((v[i * srcVStride + j] >> 2) & 0xFF);
            dst[i * dstYStride + j * 2 + 1] = (uint8_t)((u[i * srcUStride + j] >> 2) & 0xFF);
        }
    }
}

void C2SoftApvDec::process(
        const std::unique_ptr<C2Work> &work,
        const std::shared_ptr<C2BlockPool> &pool) {
    // Initialize output work
    work->result = C2_OK;
    work->workletsProcessed = 0u;
    work->worklets.front()->output.configUpdate.clear();
    work->worklets.front()->output.flags = work->input.flags;
   
    int ret = 0;
    
    size_t inOffset = 0u;
    size_t inSize = 0u;
    C2ReadView rView = mDummyReadView;
    if (!work->input.buffers.empty()) {
        rView = work->input.buffers[0]->data().linearBlocks().front().map().get();
        inSize = rView.capacity();
        if (inSize && rView.error()) {
            ALOGE("read view map failed %d", rView.error());
            work->result = C2_CORRUPTED;
            return;
        }
    }

    bool codecConfig = ((work->input.flags & C2FrameData::FLAG_CODEC_CONFIG) != 0);
    bool eos = ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0);

    ALOGV("in buffer attr. size %zu timestamp %llu frameindex %d, flags %x",
          inSize, work->input.ordinal.timestamp.peekull(),
          (int)work->input.ordinal.frameIndex.peeku(), work->input.flags);

    if (codecConfig) {
        fillEmptyWork(work);
        return;
    }

    if(inSize > 0) {
        uint8_t *bitstream = const_cast<uint8_t *>(rView.data() + inOffset);
        bitb.addr = bitstream;
        bitb.ssize = inSize;

        if (OAPV_FAILED(oapvd_info(bitstream, inSize, &aui))) {
            ALOGE("cannot get information from bitstream");
            return;
        }

        finfo = &aui.frm_info[0];

        /* create decoding frame buffers */
        ofrms.num_frms = aui.num_frms;
        for(int i = 0; i < ofrms.num_frms; i++) {
            finfo = &aui.frm_info[FRM_IDX];
            frm   = &ofrms.frm[i];

            if(mWidth != finfo->w || mHeight != finfo->w) {
                mWidth = finfo->w;
                mHeight = finfo->h;
            }

            if(frm->imgb != NULL && (frm->imgb->w[0] != finfo->w || frm->imgb->h[0] != finfo->h)) {
                frm->imgb->release(frm->imgb);
                frm->imgb = NULL;
            }

            if(frm->imgb == NULL) {
                if(outputCsp == 1) {
                    frm->imgb = imgb_create(finfo->w, finfo->h, OAPV_CS_SET(OAPV_CF_PLANAR2, 10, 0));
                }
                else {
                    frm->imgb = imgb_create(finfo->w, finfo->h, finfo->cs);
                }
                if(frm->imgb == NULL) {
                    ALOGE("cannot allocate image buffer (w:%d, h:%d, cs:%d)", finfo->w, finfo->h, finfo->cs);
                    fillEmptyWork(work);
                    return;
                }
            }
        }

        if(outputDepth == 0) {
            outputDepth = OAPV_CS_GET_BIT_DEPTH(finfo->cs);
        }

        memset(&stat, 0, sizeof(oapvd_stat_t));

        ret = oapvd_decode(oapvdHandle, &bitb, &ofrms, oapvmHandle, &stat);

        if(OAPV_FAILED(ret)) {
            ALOGE("failed to decode bitstream %d", ret);
            fillEmptyWork(work);
		    return;
        }

        status_t err = outputBuffer(pool, work);
        if (err == NOT_ENOUGH_DATA) {
            if (inSize > 0) {
                ALOGV("Maybe non-display frame at %lld.",
                    work->input.ordinal.frameIndex.peekll());
                    // send the work back with empty buffer.
                inSize = 0;
            }
        } else if (err != OK) {
            ALOGD("Error while getting the output frame out");
            // work->result would be already filled; do fillEmptyWork() below to
            // send the work back.
            inSize = 0;
        }
    }

    if (eos) {
        drainInternal(DRAIN_COMPONENT_WITH_EOS, pool, work);
        mSignalledOutputEos = true;
    } else if (!inSize) {
        fillEmptyWork(work);
    }
}

status_t C2SoftApvDec::outputBuffer(
        const std::shared_ptr<C2BlockPool> &pool,
        const std::unique_ptr<C2Work> &work)
{
    if (!(work && pool)) return BAD_VALUE;

    std::shared_ptr<C2GraphicBlock> block;
    uint32_t format = HAL_PIXEL_FORMAT_YV12;
    // uint32_t format = HAL_PIXEL_FORMAT_YCBCR_422_SP;

    C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
    c2_status_t err = pool->fetchGraphicBlock(align(mWidth, 16), mHeight, format, usage, &block);
    if (err != C2_OK) {
        ALOGE("fetchGraphicBlock for Output failed with status %d", err);
        work->result = err;
        return UNKNOWN_ERROR;
    }

    C2GraphicView wView = block->map().get();
    if (wView.error()) {
        ALOGE("graphic view map failed %d", wView.error());
        work->result = C2_CORRUPTED;
        return UNKNOWN_ERROR;
    }

    ALOGV("provided (%dx%d) required (%dx%d)",
        block->width(), block->height(), mWidth, mHeight);

    frm = &ofrms.frm[0];
    if(ofrms.num_frms > 0) {
        if(OAPV_CS_GET_BIT_DEPTH(frm->imgb->cs) != outputDepth) {
            if(imgb_w == NULL) {
                imgb_w = imgb_create(frm->imgb->w[0], frm->imgb->h[0],
                                     OAPV_CS_SET(OAPV_CS_GET_FORMAT(frm->imgb->cs), outputDepth, 0));
                if(imgb_w == NULL) {
                    ALOGE("cannot allocate image buffer (w:%d, h:%d, cs:%d)",
                           frm->imgb->w[0], frm->imgb->h[0], frm->imgb->cs);
                    finishWork(work->input.ordinal.frameIndex.peekll(), work, std::move(block));
                    return OK;
                }
            }
            imgb_cpy(imgb_w, frm->imgb);
            imgb_o = imgb_w;
        }
        else {
            imgb_o = frm->imgb;
        }
    }

    uint8_t *dst = const_cast<uint8_t *>(wView.data()[C2PlanarLayout::PLANE_Y]);
    C2PlanarLayout layout = wView.layout();
    size_t dstYStride = layout.planes[C2PlanarLayout::PLANE_Y].rowInc;
    size_t dstUVStride = layout.planes[C2PlanarLayout::PLANE_U].rowInc;
    size_t srcYStride = imgb_o->s[0];
    size_t srcUStride = imgb_o->s[1];
    size_t srcVStride = imgb_o->s[2];
    const uint8_t *srcY = (const uint8_t *)imgb_o->a[0];
    const uint8_t *srcU = (const uint8_t *)imgb_o->a[1];
    const uint8_t *srcV = (const uint8_t *)imgb_o->a[2];

    copyOutputBufferYUV422ToYUV420(
            dst, srcY, srcU, srcV,
            srcYStride, srcUStride, srcVStride,
            dstYStride, dstUVStride,
            mWidth, mHeight);

    //copyOutputBuffer10bitToYUV420(
    //        dst, srcY, srcU, srcV,
    //        srcYStride, srcUStride, srcVStride,
    //        dstYStride, dstUVStride,
    //        mWidth, mHeight);

    // copyOutputBufferToNV21(
    //         dst, srcY, srcU, srcV,
    //         srcYStride, srcUStride, srcVStride,
    //         dstYStride, dstUVStride,
    //         mWidth, mHeight);
    
    // copyOutputBufferToYuv422SP(
    //         dst, srcY, srcU, srcV,
    //         srcYStride, srcUStride, srcVStride,
    //         dstYStride, dstUVStride,
    //         mWidth, mHeight);
    //         ALOGE("  yuv 422 copy done");

    //copyOutputBufferToYuvPlanarFrame(
    //         dst, srcY, srcU, srcV,
    //         srcYStride, srcUStride, srcVStride,
    //         dstYStride, dstUVStride,
    //         mWidth, mHeight);
    //         ALOGE("  yuv 420 planar done");

    finishWork(work->input.ordinal.frameIndex.peekll(), work, std::move(block));
    
    return OK;
}

c2_status_t C2SoftApvDec::drainInternal(
        uint32_t drainMode,
        const std::shared_ptr<C2BlockPool> &pool,
        const std::unique_ptr<C2Work> &work) {
    if (drainMode == NO_DRAIN) {
        ALOGW("drain with NO_DRAIN: no-op");
        return C2_OK;
    }
    if (drainMode == DRAIN_CHAIN) {
        ALOGW("DRAIN_CHAIN not supported");
        return C2_OMITTED;
    }

    while (outputBuffer(pool, work)) {
    }

    if (drainMode == DRAIN_COMPONENT_WITH_EOS && work && work->workletsProcessed == 0u) {
        fillEmptyWork(work);
    }
    return C2_OK;
}

c2_status_t C2SoftApvDec::drain(
        uint32_t drainMode,
        const std::shared_ptr<C2BlockPool> &pool) {
    return drainInternal(drainMode, pool, nullptr);
}

class C2SoftApvDecFactory : public C2ComponentFactory {
public:
    C2SoftApvDecFactory() : mHelper(std::static_pointer_cast<C2ReflectorHelper>(
        GetCodec2PlatformComponentStore()->getParamReflector())) {
    }

    virtual c2_status_t createComponent(
            c2_node_id_t id,
            std::shared_ptr<C2Component>* const component,
            std::function<void(C2Component*)> deleter) override {
        *component = std::shared_ptr<C2Component>(
                new C2SoftApvDec(COMPONENT_NAME,
                                 id,
                                 std::make_shared<C2SoftApvDec::IntfImpl>(mHelper)),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id,
            std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new SimpleInterface<C2SoftApvDec::IntfImpl>(
                        COMPONENT_NAME, id, std::make_shared<C2SoftApvDec::IntfImpl>(mHelper)),
                deleter);
        return C2_OK;
    }

    virtual ~C2SoftApvDecFactory() override = default;

private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
};

}  // namespace android

__attribute__((cfi_canonical_jump_table))
extern "C" ::C2ComponentFactory* CreateCodec2Factory() {
    return new ::android::C2SoftApvDecFactory();
}

__attribute__((cfi_canonical_jump_table))
extern "C" void DestroyCodec2Factory(::C2ComponentFactory* factory) {
    delete factory;
}
