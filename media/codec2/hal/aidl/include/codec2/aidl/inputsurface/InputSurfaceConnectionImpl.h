/*
 * Copyright 2025, The Android Open Source Project
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

#pragma once

#include <atomic>

#include <aidl/android/media/IAidlBufferSource.h>
#include <aidl/android/media/IAidlNode.h>
#include <codec2/hidl/client.h>
#include <media/stagefright/foundation/Mutexed.h>
#include <media/stagefright/aidlpersistentsurface/C2NodeDef.h>

namespace android {

/**
 * IOmxNode implementation around codec 2.0 component, only to be used in
 * IGraphicBufferSource::configure. Only subset of IOmxNode API is implemented.
 * As a result, one cannot expect this IOmxNode to work in any other usage than
 * IGraphicBufferSource(if aidl hal is used, IAidlGraphicBufferSource).
 */
struct InputSurfaceConnectionImpl {
    explicit InputSurfaceConnectionImpl(
            const std::shared_ptr<IInputSink> &sink,
            ::android::sp<InputSurfaceSource> const &source);
    ~InputSurfaceConnectionImpl();

    void release();

    // removed
    // freeNode(), I don't see anywhere it can be called.
    // onFirstInputFrame(), we will not reuse this for multiple times.
    // getInputBufferParams(), component configuration should be retrieved from
    // client.
    void getConsumerUsageBits(uint64_t *usage /* nonnull */);
    void setConsumerUsageBits(uint64_t usage);
    void setAdjustTimestampGapUs(int32_t gapUs);

    status_t submitBuffer(
            uint32_t buffer, const sp<GraphicBuffer> &graphicBuffer,
            uint32_t flags, int64_t timestamp, int fenceFd);
    status_t onDataspaceChanged(uint32_t dataSpace, uint32_t pixelFormat);

    /**
     * Configure the frame size.
     */
    void setFrameSize(uint32_t width, uint32_t height);

    /**
     * Notify that the input buffer reference is no longer needed by the component.
     * Clean up if necessary.
     *
     * \param index input work index
     */
    void onInputBufferDone(c2_cntr64_t index);

    /**
     * Notify input buffer is emptied.
     */
    void onInputBufferEmptied();

    /**
     * Returns dataspace information from GraphicBufferSource.
     */
    android_dataspace getDataspace();

    /**
     * Returns dataspace information from GraphicBufferSource.
     */
    uint32_t getPixelFormat();

private:
    std::weak_ptr<IInputSink> mSink;
    ::android::wp<InputSurfaceSource> mSource;
    c2_status_t mInit;
    std::atomic<bool> mReleased;

    std::shared_ptr<C2Allocator> mAllocator;
    std::atomic_uint64_t mFrameIndex;
    uint32_t mWidth;
    uint32_t mHeight;
    uint64_t mUsage;
    Mutexed<android_dataspace> mDataspace;
    Mutexed<uint32_t> mPixelFormat;

    // WORKAROUND: timestamp adjustment

    // if >0: this is the max timestamp gap, if <0: this is -1 times the fixed timestamp gap
    // if 0: no timestamp adjustment is made
    // note that C2OMXNode can be recycled between encoding sessions.
    int32_t mAdjustTimestampGapUs;
    bool mFirstInputFrame; // true for first input
    c2_cntr64_t mPrevInputTimestamp; // input timestamp for previous frame
    c2_cntr64_t mPrevCodecTimestamp; // adjusted (codec) timestamp for previous frame

    // Tracks the status of buffers
    struct BuffersTracker {
        BuffersTracker() = default;

        // Keeps track of buffers that are used by the component. Maps timestamp -> ID
        std::map<uint64_t, uint32_t> mIdsInUse;
        // Keeps track of the buffer IDs that are available after being released from the component.
        std::list<uint32_t> mAvailableIds;
    };
    Mutexed<BuffersTracker> mBuffersTracker;

    class QueueThread;
    sp<QueueThread> mQueueThread;

    void notifyInputBufferEmptied(int32_t bufferId);
};

}  // namespace android
