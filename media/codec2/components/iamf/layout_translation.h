/*
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

#ifndef ANDROID_C2_IAMF_LAYOUT_TRANSLATION_H_

#include <C2.h>
#include "iamf/api/types.h"

namespace android {
namespace c2_soft_iamf_internal {

// TODO: From C2Param.h:
// "It should change "invalid/unsupported" values to the nearest supported values."
// "This is communicated to the client by changing the source values in tune()/
// configure()."

// Translate the requested output ChannelMask into one of the IAMF Layouts.
// ChannelMasks not matching an IAMF Layout are rejected, but CHANNEL_OUT_5POINT1POINT2 and
// CHANNEL_OUT_7POINT1POINT2 are matched to their near equivalents. IAMF Layouts, from spec 1.0.1:
// https://aomediacodec.github.io/iamf/#loudspeaker_layout
c2_status_t GetIamfLayout(uint32_t channelMask, iamf_tools::api::OutputLayout& output_layout);

}  // namespace c2_soft_iamf_internal
}  // namespace android

#endif  // ANDROID_C2_IAMF_LAYOUT_TRANSLATION_H_
