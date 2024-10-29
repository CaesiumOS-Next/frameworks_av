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

//#define LOG_NDEBUG 0
#define LOG_TAG "NdkMediaCodecInfo"

#include "NdkMediaCodecInfoPriv.h"

#include <media/NdkMediaCodecInfo.h>
#include <media/NdkMediaFormatPriv.h>

using namespace android;

extern "C" {

// AMediaCodecInfo

EXPORT
const char* AMediaCodecInfo_getCanonicalName(const AMediaCodecInfo *info) {
    if (info == nullptr || info->mInfo == nullptr) {
        return nullptr;
    }

    return info->mInfo->getCodecName();
}

EXPORT
bool AMediaCodecInfo_isEncoder(const AMediaCodecInfo *info) {
    return info->mInfo->isEncoder();
}

EXPORT
bool AMediaCodecInfo_isVendor(const AMediaCodecInfo *info) {
    int32_t attributes = info->mInfo->getAttributes();
    return (attributes & android::MediaCodecInfo::kFlagIsVendor);
}

EXPORT
AMediaCodecType AMediaCodecInfo_getMediaCodecInfoType(const AMediaCodecInfo *info) {
    if (info == nullptr || info->mInfo == nullptr) {
        return (AMediaCodecType)0;
    }

    int32_t attributes = info->mInfo->getAttributes();

    if (attributes & android::MediaCodecInfo::kFlagIsSoftwareOnly) {
        return SOFTWARE_ONLY;
    }
    if (attributes & android::MediaCodecInfo::kFlagIsHardwareAccelerated) {
        return HARDWARE_ACCELERATED;
    }
    return UNKNOWN;
}

EXPORT
media_status_t AMediaCodecInfo_getMediaType(const AMediaCodecInfo *info, const char **outMediaType) {
    if (info == nullptr || info->mInfo == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outMediaType = info->mMediaType;

    return AMEDIA_OK;
}

EXPORT
int32_t AMediaCodecInfo_getMaxSupportedInstances(const AMediaCodecInfo *info) {
    return info->mCodecCaps->getMaxSupportedInstances();
}

EXPORT
bool AMediaCodecInfo_isFeatureSupported(const AMediaCodecInfo *info, const char *featureName) {
    return info->mCodecCaps->isFeatureSupported(std::string(featureName));
}

EXPORT
bool AMediaCodecInfo_isFeatureRequired(const AMediaCodecInfo *info, const char *featureName) {
    return info->mCodecCaps->isFeatureRequired(std::string(featureName));
}

EXPORT
bool AMediaCodecInfo_isFormatSupported(const AMediaCodecInfo *info, const AMediaFormat *format) {
    sp<AMessage> nativeFormat;
    AMediaFormat_getFormat(format, &nativeFormat);

    return info->mCodecCaps->isFormatSupported(nativeFormat);
}

EXPORT
media_status_t AMediaCodecInfo_getAudioCapabilities(const AMediaCodecInfo *info,
        const ACodecAudioCapabilities **outAudioCaps) {
    if (info == nullptr || info->mInfo == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outAudioCaps = info->mAAudioCaps.get();
    return AMEDIA_OK;
}

// ACodecAudioCapabilities

EXPORT
media_status_t ACodecAudioCapabilities_getBitrateRange(const ACodecAudioCapabilities *audioCaps,
        const AIntRange **outRange) {
    if (audioCaps == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outRange = audioCaps->mBitrateRange.get();

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecAudioCapabilities_getSupportedSampleRates(const ACodecAudioCapabilities *audioCaps,
        const int **outArrayPtr) {
    if (audioCaps == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    const std::vector<int>& sampleRates = audioCaps->mAudioCaps->getSupportedSampleRates();

    if (sampleRates.empty()) {
        return AMEDIA_ERROR_UNSUPPORTED;
    }

    *outArrayPtr = sampleRates.data();

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecAudioCapabilities_getSupportedSampleRateRanges(
        const ACodecAudioCapabilities *audioCaps, const AIntRange **outArrayPtr) {
    if (audioCaps == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outArrayPtr = audioCaps->mSampleRateRanges.data();

    return AMEDIA_OK;
}

EXPORT
int32_t ACodecAudioCapabilities_getMaxInputChannelCount(const ACodecAudioCapabilities *audioCaps) {
    return audioCaps->mAudioCaps->getMaxInputChannelCount();
}

EXPORT
int32_t ACodecAudioCapabilities_getMinInputChannelCount(const ACodecAudioCapabilities *audioCaps) {
    return audioCaps->mAudioCaps->getMinInputChannelCount();
}

EXPORT
media_status_t ACodecAudioCapabilities_getInputChannelCountRanges(
        const ACodecAudioCapabilities *audioCaps, const AIntRange **outArrayPtr) {
    if (audioCaps == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outArrayPtr = audioCaps->mInputChannelCountRanges.data();

    return AMEDIA_OK;
}

EXPORT
bool ACodecAudioCapabilities_isSampleRateSupported(const ACodecAudioCapabilities *audioCaps,
        int32_t sampleRate) {
    return audioCaps->mAudioCaps->isSampleRateSupported(sampleRate);
}

}