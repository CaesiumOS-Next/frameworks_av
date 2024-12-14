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
#include <media/stagefright/CCodec.h>
#include <media/stagefright/MediaCodecConstants.h>

#include <C2BlockInternal.h>
#include <C2PlatformSupport.h>
#include <Codec2Mapper.h>

namespace android {

TEST(CodecResourcesTest, SystemResources) {
    std::vector<GlobalResourceInfo> globalResources = CCodec::GetGloballyAvailableResources();

    ALOGI("GloballyAvailableResource count is: %zu", globalResources.size());
    for (const auto& resource : globalResources) {
        ALOGI("Resource[%s: %jd: %jd]",
              resource.mName.c_str(), resource.mCapacity, resource.mAvailable);
    }
}

} // namespace android
