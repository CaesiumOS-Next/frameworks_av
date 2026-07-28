/*
 * Copyright (C) 2026 The Android Open Source Project
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

#define LOG_TAG "GraphicsTrackerLogger"
#include <codec2/aidl/GraphicsTrackerLogger.h>
#include <log/log.h>
#include <cinttypes>

namespace aidl::android::hardware::media::c2::implementation {

GraphicsTrackerLogger::GraphicsTrackerLogger(const std::string& name, const void* ownerPtr,
                                             std::function<int()> dequeueableGetter,
                                             std::chrono::seconds interval)
    : mName(name), mOwnerPtr(ownerPtr), mDequeueableGetter(std::move(dequeueableGetter)), mInterval(interval) {}

GraphicsTrackerLogger::~GraphicsTrackerLogger() {
    stop();
}

void GraphicsTrackerLogger::start() {
    bool expected = false;
    if (mRunning.compare_exchange_strong(expected, true)) {
        mThread = std::thread([this]() { loop(); });
    }
}

void GraphicsTrackerLogger::stop() {
    if (mRunning.exchange(false)) {
        mCv.notify_all();
        if (mThread.joinable()) {
            mThread.join();
        }
        flush();
    }
}

void GraphicsTrackerLogger::flush() {
    bool enabled = ::android::base::GetBoolProperty("debug.c2.graphics_tracker.log", true);
    if (!enabled) {
        return;
    }

    uint64_t curAllocate = mCountAllocate.load(std::memory_order_relaxed);
    uint64_t curRender = mCountRender.load(std::memory_order_relaxed);
    uint64_t curDeallocate = mCountDeallocate.load(std::memory_order_relaxed);
    uint64_t curReleased = mCountReleased.load(std::memory_order_relaxed);

    uint64_t deltaAllocate = curAllocate - mLastReportedAllocate;
    uint64_t deltaRender = curRender - mLastReportedRender;
    uint64_t deltaDeallocate = curDeallocate - mLastReportedDeallocate;
    uint64_t deltaReleased = curReleased - mLastReportedReleased;

    if (deltaAllocate > 0 || deltaRender > 0 || deltaDeallocate > 0 || deltaReleased > 0) {
        uint64_t activeAllocated = (curAllocate > (curRender + curDeallocate))
                                    ? (curAllocate - curRender - curDeallocate) : 0;
        int dequeueable = mDequeueableGetter ? mDequeueableGetter() : -1;

        ALOGI("[%s %p] Periodic Stats (last %llds / Total) - "
              "ActiveAllocated: %" PRIu64 ", Dequeueable: %d, "
              "Allocate: +%" PRIu64 " (total %" PRIu64 "), "
              "Render: +%" PRIu64 " (total %" PRIu64 "), "
              "Deallocate: +%" PRIu64 " (total %" PRIu64 "), "
              "Released: +%" PRIu64 " (total %" PRIu64 ")",
              mName.c_str(), mOwnerPtr,
              static_cast<long long>(mInterval.count()),
              activeAllocated, dequeueable,
              deltaAllocate, curAllocate,
              deltaRender, curRender,
              deltaDeallocate, curDeallocate,
              deltaReleased, curReleased);

        mLastReportedAllocate = curAllocate;
        mLastReportedRender = curRender;
        mLastReportedDeallocate = curDeallocate;
        mLastReportedReleased = curReleased;
    }
}

void GraphicsTrackerLogger::loop() {
    while (mRunning) {
        {
            std::unique_lock<std::mutex> lock(mLock);
            mCv.wait_for(lock, mInterval, [this]() {
                return !mRunning.load();
            });
        }

        flush();
    }
    flush();
}

} // namespace aidl::android::hardware::media::c2::implementation
