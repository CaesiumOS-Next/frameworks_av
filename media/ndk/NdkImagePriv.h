/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef _NDK_IMAGE_PRIV_H
#define _NDK_IMAGE_PRIV_H

#include <inttypes.h>
#include <utils/Log.h>
#include <utils/StrongPointer.h>

#include <gui/BufferItem.h>
#include <gui/CpuConsumer.h>

#include "NdkImageReaderPriv.h"
#include <media/NdkImage.h>


using namespace android;

// Formats not listed in the public API, but still available to AImageReader
enum AIMAGE_PRIVATE_FORMATS {
    /**
     * Unprocessed implementation-dependent raw
     * depth measurements, opaque with 16 bit
     * samples.
     *
     */

    AIMAGE_FORMAT_RAW_DEPTH = 0x1002,

    /**
     * Device specific 10 bits depth RAW image format.
     *
     * <p>Unprocessed implementation-dependent raw depth measurements, opaque with 10 bit samples
     * and device specific bit layout.</p>
     */
    AIMAGE_FORMAT_RAW_DEPTH10 = 0x1003,
};

// TODO: this only supports ImageReader
struct AImage {
    AImage(AImageReader* reader, int32_t format, uint64_t usage, BufferItem* buffer,
           int64_t timestamp, int32_t width, int32_t height, int32_t numPlanes);

    // free all resources while keeping object alive. Caller must obtain reader lock
    void close() { close(-1); }
    void close(int releaseFenceFd);

    // Remove from object memory. Must be called after close
    void free();

    bool isClosed() const ;

    // only For AImage to grab reader lock
    // Always grab reader lock before grabbing image lock
    void lockReader() const;
    void unlockReader() const;

    media_status_t getWidth(/*out*/int32_t* width) const;
    media_status_t getHeight(/*out*/int32_t* height) const;
    media_status_t getFormat(/*out*/int32_t* format) const;
    media_status_t getNumPlanes(/*out*/int32_t* numPlanes) const;
    media_status_t getTimestamp(/*out*/int64_t* timestamp) const;

    media_status_t lockImage();
    media_status_t unlockImageIfLocked(/*out*/int* fenceFd);

    media_status_t getPlanePixelStride(int planeIdx, /*out*/int32_t* pixelStride) const;
    media_status_t getPlaneRowStride(int planeIdx, /*out*/int32_t* rowStride) const;
    media_status_t getPlaneData(int planeIdx,/*out*/uint8_t** data, /*out*/int* dataLength) const;
    media_status_t getHardwareBuffer(/*out*/AHardwareBuffer** buffer) const;
    media_status_t getDataSpace(/*out*/android_dataspace* dataSpace) const;

    /**
     * setFilteringEnabled sets whether the transform matrix should be computed
     * for use with bilinear filtering.
     */
    media_status_t setFilteringEnabled(bool enabled);

    /**
     * getTransformMatrix retrieves the 4x4 texture coordinate transform matrix
     * associated with the image set by the most recent call to updateTexImage.
     *
     * This transform matrix maps 2D homogeneous texture coordinates of the form
     * (s, t, 0, 1) with s and t in the inclusive range [0, 1] to the texture
     * coordinate that should be used to sample that location from the texture.
     * Sampling the texture outside of the range of this transform is undefined.
     *
     * This transform is necessary to compensate for transforms that the stream
     * content producer may implicitly apply to the content. By forcing users of
     * an AImage to apply this transform we avoid performing an extracopy of the
     * data that would be needed to hide the transform from the user.
     *
     * The matrix is stored in column-major order so that it may be passed
     * directly to OpenGL ES via the glLoadMatrixf or glUniformMatrix4fv
     * functions.
     */
    media_status_t getTransformMatrix(float mtx[16]);

    /**
     * Computes the transform matrix documented by getTransformMatrix
     * from the BufferItem sub parts.
     */
    static void computeTransformMatrix(float outTransform[16], const sp<GraphicBuffer>& buf,
                                       const Rect& cropRect, uint32_t transform);

    /**
     * computeCurrentTransformMatrixLocked computes the transform matrix for the
     * current image and stores it in mCurrentTransformMatrix.
     */
    media_status_t computeCurrentTransformMatrixLocked();
  
  private:
    // AImage should be deleted through free() API.
    ~AImage();

    friend struct AImageReader; // for reader to access mBuffer

    uint32_t getJpegSize() const;

    // When reader is close, AImage will only accept close API call
    const sp<AImageReader>     mReader;
    const int32_t              mFormat;
    const uint64_t             mUsage;  // AHARDWAREBUFFER_USAGE_* flags.
    BufferItem*                mBuffer;
    std::unique_ptr<CpuConsumer::LockedBuffer> mLockedBuffer;
    const int64_t              mTimestamp;
    const int32_t              mWidth;
    const int32_t              mHeight;
    const int32_t              mNumPlanes;
    android_dataspace          mHalDataSpace = HAL_DATASPACE_UNKNOWN;
    bool                       mIsClosed = false;
    mutable Mutex              mLock;

    /**
     * mTransformMatrixComputed indicates whether the transform matrix has been
     * computed.
     */
    bool mTransformMatrixComputed;

    /**
     * mCurrentTransformMatrix is the transform matrix for the current image.
     * It gets computed by computeTransformMatrix when the image is created.
     */
    float mCurrentTransformMatrix[16];
};

#endif // _NDK_IMAGE_PRIV_H
