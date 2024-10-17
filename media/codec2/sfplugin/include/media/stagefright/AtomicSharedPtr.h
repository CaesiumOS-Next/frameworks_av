/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <memory>
#include <mutex>

#include <media/stagefright/foundation/ABase.h>

namespace android {

// The class guarantees thready safety of reset() for the carrying std::shared_ptr<T>
// by utilizing std::mutex.
//
// TODO: Replace uses of this with std::atomic<std::shared_ptr> which was
// introduced from c++20.
template <typename T>
class AtomicSharedPtr {
public:
    AtomicSharedPtr() = default;
    ~AtomicSharedPtr() = default;

    std::shared_ptr<T> get() {
        std::shared_ptr<T> ret;
        {
            std::unique_lock<std::mutex> l(mLock);
            ret = mPtr;
        }
        return ret;
    }

    void set(const std::shared_ptr<T> &newPtr) {
        std::unique_lock<std::mutex> l(mLock);
        mPtr = newPtr;
    }

    void reset() {
        std::unique_lock<std::mutex> l(mLock);
        mPtr.reset();
    }

private:
    std::mutex mLock;
    std::shared_ptr<T> mPtr;

    DISALLOW_EVIL_CONSTRUCTORS(AtomicSharedPtr<T>);
};

}  // namespace android
