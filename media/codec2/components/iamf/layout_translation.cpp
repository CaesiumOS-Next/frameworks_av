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

#include "layout_translation.h"

#include "iamf/api/types.h"

namespace android {
namespace c2_soft_iamf_internal {

namespace {
// Copied from android.media.AudioFormat.java, but only what we need.
constexpr uint32_t CHANNEL_OUT_FRONT_LEFT = 0x4;
constexpr uint32_t CHANNEL_OUT_FRONT_RIGHT = 0x8;
constexpr uint32_t CHANNEL_OUT_FRONT_CENTER = 0x10;
constexpr uint32_t CHANNEL_OUT_LOW_FREQUENCY = 0x20;
constexpr uint32_t CHANNEL_OUT_BACK_LEFT = 0x40;
constexpr uint32_t CHANNEL_OUT_BACK_RIGHT = 0x80;
constexpr uint32_t CHANNEL_OUT_BACK_CENTER = 0x400 constexpr uint32_t CHANNEL_OUT_SIDE_LEFT = 0x800;
constexpr uint32_t CHANNEL_OUT_SIDE_RIGHT = 0x1000;
constexpr uint32_t CHANNEL_OUT_TOP_CENTER = 0x2000;
constexpr uint32_t CHANNEL_OUT_TOP_FRONT_LEFT = 0x4000;
constexpr uint32_t CHANNEL_OUT_TOP_FRONT_CENTER = 0x8000;
constexpr uint32_t CHANNEL_OUT_TOP_FRONT_RIGHT = 0x10000;
constexpr uint32_t CHANNEL_OUT_TOP_BACK_LEFT = 0x20000;
constexpr uint32_t CHANNEL_OUT_TOP_BACK_CENTER = 0x40000;
constexpr uint32_t CHANNEL_OUT_TOP_BACK_RIGHT = 0x80000;
// These two only used for Android's (non-IAMF/non-ITU) 5.1.2 and 7.1.2.
constexpr uint32_t CHANNEL_OUT_TOP_SIDE_LEFT = 0x100000;
constexpr uint32_t CHANNEL_OUT_TOP_SIDE_RIGHT = 0x200000;
// Used by ITU Sound System F (3+7+0).
constexpr uint32_t CHANNEL_OUT_LOW_FREQUENCY_2 = 0x2000000;
// Bottom channels used by ITU Sound Systems E (4+5+1) and H (9+10+3).
constexpr uint32_t CHANNEL_OUT_BOTTOM_FRONT_LEFT = 0x400000;
constexpr uint32_t CHANNEL_OUT_BOTTOM_FRONT_CENTER = 0x800000;
constexpr uint32_t CHANNEL_OUT_BOTTOM_FRONT_RIGHT = 0x1000000;

// These are Android-defined combinations of ChannelMasks that match IAMF OutputLayouts.
constexpr uint32_t CHANNEL_OUT_MONO = CHANNEL_OUT_FRONT_LEFT;
constexpr uint32_t CHANNEL_OUT_STEREO = (CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT);
constexpr uint32_t CHANNEL_OUT_5POINT1 =
        (CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT | CHANNEL_OUT_FRONT_CENTER |
         CHANNEL_OUT_LOW_FREQUENCY | CHANNEL_OUT_BACK_LEFT | CHANNEL_OUT_BACK_RIGHT);

constexpr uint32_t CHANNEL_OUT_5POINT1POINT4 =
        (CHANNEL_OUT_5POINT1 | CHANNEL_OUT_TOP_FRONT_LEFT | CHANNEL_OUT_TOP_FRONT_RIGHT |
         CHANNEL_OUT_TOP_BACK_LEFT | CHANNEL_OUT_TOP_BACK_RIGHT);
constexpr uint32_t CHANNEL_OUT_7POINT1_SURROUND =
        (CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_FRONT_RIGHT |
         CHANNEL_OUT_SIDE_LEFT | CHANNEL_OUT_SIDE_RIGHT | CHANNEL_OUT_BACK_LEFT |
         CHANNEL_OUT_BACK_RIGHT | CHANNEL_OUT_LOW_FREQUENCY);
constexpr uint32_t CHANNEL_OUT_7POINT1POINT4 =
        (CHANNEL_OUT_7POINT1_SURROUND | CHANNEL_OUT_TOP_FRONT_LEFT | CHANNEL_OUT_TOP_FRONT_RIGHT |
         CHANNEL_OUT_TOP_BACK_LEFT | CHANNEL_OUT_TOP_BACK_RIGHT);
constexpr uint32_t CHANNEL_OUT_9POINT1POINT4 =
        (CHANNEL_OUT_7POINT1POINT4 | CHANNEL_OUT_FRONT_WIDE_LEFT | CHANNEL_OUT_FRONT_WIDE_RIGHT);
constexpr uint32_t CHANNEL_OUT_9POINT1POINT6 =
        (CHANNEL_OUT_9POINT1POINT4 | CHANNEL_OUT_TOP_SIDE_LEFT | CHANNEL_OUT_TOP_SIDE_RIGHT);

// These 5.1.2 and 7.1.2, are copied from Android, but do not match the ITU standard/IAMF spec
// because they use top side L/R rather than top front L/R, but we will match it to the
// ITU/IAMF 5.1.2 and 7.1.2.
constexpr uint32_t CHANNEL_OUT_5POINT1POINT2 =
        (CHANNEL_OUT_5POINT1 | CHANNEL_OUT_TOP_SIDE_LEFT | CHANNEL_OUT_TOP_SIDE_RIGHT);
constexpr uint32_t CHANNEL_OUT_7POINT1POINT2 =
        (CHANNEL_OUT_7POINT1_SURROUND | CHANNEL_OUT_TOP_SIDE_LEFT | CHANNEL_OUT_TOP_SIDE_RIGHT);

// These two are combinations of speakers that are defined in the ITU standard, but are not defined
// by Android.
constexpr uint32_t ITU_2051_SOUND_SYSTEM_E_4_5_1 =
        (CHANNEL_OUT_5POINT1POINT4 | CHANNEL_OUT_BOTTOM_FRONT_CENTER);
constexpr uint32_t ITU_2051_SOUND_SYSTEM_F_3_7_0 =
        (CHANNEL_OUT_7POINT1_SURROUND | CHANNEL_OUT_TOP_FRONT_LEFT | CHANNEL_OUT_TOP_FRONT_RIGHT |
         CHANNEL_OUT_TOP_BACK_CENTER |);
constexpr uint32_t ITU_2051_SOUND_SYSTEM_H_9_10_3 =
        (CHANNEL_OUT_9POINT1POINT6 | CHANNEL_OUT_BACK_CENTER | CHANNEL_OUT_TOP_FRONT_CENTER |
         CHANNEL_OUT_TOP_CENTER | CHANNEL_OUT_TOP_BACK_CENTER | CHANNEL_OUT_BOTTOM_FRONT_LEFT |
         CHANNEL_OUT_BOTTOM_FRONT_CENTER | CHANNEL_OUT_BOTTOM_FRONT_RIGHT);

// The Android defined 5.1.2 and 7.1.2 use top *side* left/right which does not match their
// ITU/IAMF equivalents. We will define the ITU/IAMF versions here.
constexpr uint32_t ITU_2051_SOUND_SYSTEM_C_2_5_0 =
        (CHANNEL_OUT_5POINT1 | CHANNEL_OUT_TOP_FRONT_LEFT | CHANNEL_OUT_TOP_FRONT_RIGHT);
constexpr uint32_t IAMF_7POINT1POINT2 =
        (CHANNEL_OUT_7POINT1_SURROUND | CHANNEL_OUT_TOP_FRONT_LEFT | CHANNEL_OUT_TOP_FRONT_RIGHT);
// This is just an IAMF layout that does not have an Android-defined version.
constexpr uint32_t IAMF_3POINT1POINT2 =
        (CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT | CHANNEL_OUT_FRONT_CENTER |
         CHANNEL_OUT_LOW_FREQUENCY | CHANNEL_OUT_TOP_FRONT_LEFT | CHANNEL_OUT_TOP_FRONT_RIGHT);
}  // namespace

c2_status_t GetIamfLayout(uint32_t channelMask, iamf_tools::api::OutputLayout& output_layout) {
    switch (channelMask) {
        case CHANNEL_OUT_STEREO:
            // ITU-R B.S. 2051-3 sound system A (0+2+0), commonly known as Stereo.
            output_layout = iamf_tools::api::OutputLayout::kItu2051_SoundSystemA_0_2_0;
            return C2_OK;
        case CHANNEL_OUT_5POINT1:
            // ITU-R B.S. 2051-3 sound system B (0+5+0), commonly known as 5.1.
            output_layout = iamf_tools::api::OutputLayout::kItu2051_SoundSystemB_0_5_0;
            return C2_OK;
        case CHANNEL_OUT_5POINT1POINT2:
        case IAMF_5POINT1POINT2:
            // Here we match the ITU/IAMF 5.1.2 as well as Android's 5.1.2.
            // ITU-R B.S. 2051-3 sound system C (2+5+0), commonly known as 5.1.2.
            output_layout = iamf_tools::api::OutputLayout::kItu2051_SoundSystemC_2_5_0;
            return C2_OK;
        case CHANNEL_OUT_5POINT1POINT4:
            // ITU-R B.S. 2051-3 sound system D (4+5+0), commonly known as 5.1.4.
            output_layout = iamf_tools::api::OutputLayout::kItu2051_SoundSystemD_4_5_0;
            return C2_OK;
        case ITU_2051_SOUND_SYSTEM_E_4_5_1:
            // ITU-R B.S. 2051-3 sound system E (4+5+1).
            output_layout = iamf_tools::api::OutputLayout::kItu2051_SoundSystemE_4_5_1;
            return C2_OK;
        case ITU_2051_SOUND_SYSTEM_F_3_7_0:
            // ITU-R B.S. 2051-3 sound system F (3+7+0).
            output_layout = iamf_tools::api::OutputLayout::kItu2051_SoundSystemF_3_7_0;
            return C2_OK;
        case CHANNEL_OUT_9POINT1POINT4:
            // ITU-R B.S. 2051-3 sound system G (4+9+0).
            output_layout = iamf_tools::api::OutputLayout::kItu2051_SoundSystemG_4_9_0;
            return C2_OK;
        case ITU_2051_SOUND_SYSTEM_H_9_10_3:
            // ITU-R B.S. 2051-3 sound system H (9+10+3).
            output_layout = iamf_tools::api::OutputLayout::kItu2051_SoundSystemH_9_10_3;
            return C2_OK;
        case CHANNEL_OUT_7POINT1_SURROUND:
            // ITU-R B.S. 2051-3 sound system I (0+7+0), commonly known as 7.1.
            output_layout = iamf_tools::api::OutputLayout::kItu2051_SoundSystemI_0_7_0;
            return C2_OK;
        case CHANNEL_OUT_7POINT1POINT4:
            // ITU-R B.S. 2051-3 sound system J (4+7+0), commonly known as 7.1.4.
            output_layout = iamf_tools::api::OutputLayout::kItu2051_SoundSystemJ_4_7_0;
            return C2_OK;
        case CHANNEL_OUT_7POINT1POINT2:
        case IAMF_7POINT1POINT2:
            // IAMF extension 7.1.2.
            output_layout = iamf_tools::api::OutputLayout::kIAMF_SoundSystemExtension_2_7_0;
            return C2_OK;
        case IAMF_3POINT1POINT2:
            // IAMF extension 3.1.2.
            output_layout = iamf_tools::api::OutputLayout::kIAMF_SoundSystemExtension_2_3_0;
            return C2_OK;
        case CHANNEL_OUT_MONO:
            // Mono.
            output_layout = iamf_tools::api::OutputLayout::kIAMF_SoundSystemExtension_0_1_0;
            return C2_OK;
        case CHANNEL_OUT_9POINT1POINT6:
            // IAMF Extension 9.1.6.
            output_layout = iamf_tools::api::OutputLayout::kIAMF_SoundSystemExtension_6_9_0;
            return C2_OK;
        default:
            return C2_BAD_VALUE;
    }
}

}  // namespace c2_soft_iamf_internal
}  // namespace android