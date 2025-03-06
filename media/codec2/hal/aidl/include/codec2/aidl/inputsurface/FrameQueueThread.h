/*
 *
 * Copyright (C) 2025 The Android Open Source Project
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

#include <sys/types.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <thread>

#include <aidl/android/hardware/media/c2/BnInputSink.h>
#include <media/stagefright/foundation/ColorUtils.h>
#include <ui/Fence.h>
#include <utils/AndroidThreads.h>

#include <C2Config.h>
#include <C2Work.h>
#include <Codec2Mapper.h>
#include <codec2/aidl/BufferTypes.h>

namespace aidl::android::hardware::media::c2::implementation {

/**
 * This class runs a thread which receives receives encoder frames and queues them
 * to a encoder component. Frames queued in a very short duration can be batched
 * for queueing to a encoder component.
 */
class FrameQueueThread {
public:
    FrameQueueThread(const std::shared_ptr<IInputSink> &sink) : mSink{sink} {
        mThread = std::thread(&FrameQueueThread::run, this);
    }
    ~FrameQueueThread() {
        {
            std::unique_lock<std::mutex> l(mLock);
            mDone = true;
            mCv.notify_all();
        }
        if (mThread.joinable()) {
            mThread.join();
        }
    }

    /**
     * Queue a frame for an encoder.
     */
    void queue(std::unique_ptr<C2Work> &&work, int fenceFd) {
        {
            std::unique_lock<std::mutex> l(mLock);
            mItems.emplace_back(std::move(work), fenceFd);
        }
        mCv.notify_all();
    }

    /**
     * Set/update a dataspace for upcoming frames.
     */
    void setDataspace(android_dataspace dataspace) {
        std::unique_lock<std::mutex> l(mLock);
        ::android::ColorUtils::convertDataSpaceToV0(dataspace);
        mConfigUpdate.emplace_back(new C2StreamDataSpaceInfo::input(0u, dataspace));
        int32_t standard;
        int32_t transfer;
        int32_t range;
        ::android::ColorUtils::getColorConfigFromDataSpace(dataspace, &range, &standard, &transfer);
        std::unique_ptr<C2StreamColorAspectsInfo::input> colorAspects =
            std::make_unique<C2StreamColorAspectsInfo::input>(0u);
        if (::android::C2Mapper::map(standard, &colorAspects->primaries, &colorAspects->matrix)
                && ::android::C2Mapper::map(transfer, &colorAspects->transfer)
                && ::android::C2Mapper::map(range, &colorAspects->range)) {
            mConfigUpdate.push_back(std::move(colorAspects));
        }
    }

    /**
     * Set/update thread priority for the frame queueing thread.
     */
    void setPriority(int priority) {
        androidSetThreadPriority(gettid(), priority);
    }
private:
    bool mDone = false;
    std::thread mThread;
    std::weak_ptr<IInputSink> mSink;

    std::mutex mLock;
    std::condition_variable mCv;
    struct Item {
        Item(std::unique_ptr<C2Work> &&w, int fd) : work(std::move(w)), fenceFd(fd) {}

        std::unique_ptr<C2Work> work;
        int fenceFd;
    };
    std::deque<Item> mItems;
    std::deque<std::unique_ptr<C2Param>> mConfigUpdate;
    nsecs_t mLastQueuedTimestampNs = 0;

private:
    void run() {
        constexpr nsecs_t kIntervalNs = nsecs_t(10) * 1000 * 1000;  // 10ms
        constexpr nsecs_t kWaitNs = kIntervalNs * 2;

        std::unique_lock<std::mutex> lock(mLock);
        while (!mDone) {
            nsecs_t nowNs = systemTime();
            nsecs_t diffNs = nowNs - mLastQueuedTimestampNs;
            if (mItems.empty() || (mLastQueuedTimestampNs != 0 && diffNs < kIntervalNs)) {
                mCv.wait_for(lock, std::chrono::nanoseconds(kIntervalNs - diffNs));
                continue;
            }
            std::deque<Item> items = std::move(mItems);
            std::deque<std::unique_ptr<C2Param>> configUpdate = std::move(mConfigUpdate);
            lock.unlock();
            queueItems(items, configUpdate);
            lock.lock();
            mLastQueuedTimestampNs = nowNs;
            mCv.wait_for(lock, std::chrono::nanoseconds(kWaitNs));
        }
    }

    void queueItems(
            std::deque<Item> &items,
            std::deque<std::unique_ptr<C2Param>> &configUpdate) {
        std::shared_ptr<IInputSink> sink = mSink.lock();
        if (!sink) {
            return;
        }

        std::list<std::unique_ptr<C2Work>> c2Items;
        std::vector<int> fenceFds;
        while (!items.empty()) {
            c2Items.push_back(std::move(items.front().work));
            fenceFds.push_back(items.front().fenceFd);
            items.pop_front();
        }
        for (const std::unique_ptr<C2Param> &param: configUpdate) {
            c2Items.front()->input.configUpdate.emplace_back(C2Param::Copy(*param));
        }
        for (int fenceFd : fenceFds) {
            ::android::sp<::android::Fence> fence(new ::android::Fence(fenceFd));
            fence->waitForever(LOG_TAG);
        }

        WorkBundle workBundle;
        // TODO: log error;
        if (utils::ToAidl(&workBundle, c2Items, nullptr)) {
            sink->queue(workBundle);
        }
    }
};

}  // namespace aidl::android::hardware::media::c2::implementation
