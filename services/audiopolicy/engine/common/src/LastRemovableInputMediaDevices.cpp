/*
 * Copyright (C) 2019 The Android Open Source Project
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

#define LOG_TAG "APM::AudioPolicyEngine/LastRemovableInputMediaDevices"
//#define LOG_NDEBUG 0

#include "LastRemovableInputMediaDevices.h"
#include <log/log.h>

namespace android {

void LastRemovableInputMediaDevices::setRemovableInputMediaDevices(sp<DeviceDescriptor> desc,
                                                         audio_policy_dev_state_t state)
{
    if (desc == nullptr) {
        return;
    } else {
        if (state == AUDIO_POLICY_DEVICE_STATE_AVAILABLE) {
            setRemovableInputMediaDevices(desc, AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE);
            mMediaDevices.insert(mMediaDevices.begin(), desc);
        } else if (state == AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE) {
            for (auto iter = mMediaDevices.begin(); iter != mMediaDevices.end(); ++iter) {
                if ((*iter)->equals(desc)) {
                    mMediaDevices.erase(iter);
                    break;
                }
            }
        }
    }
}

std::vector<audio_devices_t> LastRemovableInputMediaDevices::getLastRemovableInputMediaDevices() const
{
    std::vector<audio_devices_t> ret;
    for (auto iter = mMediaDevices.begin(); iter != mMediaDevices.end(); ++iter) {
        ret.push_back((*iter)->type());
    }
    return ret;
}

} // namespace android
