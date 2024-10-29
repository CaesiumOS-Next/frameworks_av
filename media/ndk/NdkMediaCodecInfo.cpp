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

// Utils

EXPORT
media_status_t AIntRange_delete(AIntRange *range) {
    if (range == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    delete range;

    return AMEDIA_OK;
}

EXPORT
media_status_t ADoubleRange_delete(ADoubleRange *range) {
    if (range == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    delete range;

    return AMEDIA_OK;
}

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
media_status_t AMediaCodecInfo_getMediaType(const AMediaCodecInfo *info,
        const char **outMediaType) {
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

    if ((*outAudioCaps) == nullptr) {
        return AMEDIA_ERROR_UNSUPPORTED;
    }

    return AMEDIA_OK;
}

EXPORT
media_status_t AMediaCodecInfo_getVideoCapabilities(const AMediaCodecInfo *info,
        const ACodecVideoCapabilities **outVideoCaps) {
    if (info == nullptr || info->mInfo == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outVideoCaps = info->mAVideoCaps.get();

    if ((*outVideoCaps) == nullptr) {
        return AMEDIA_ERROR_UNSUPPORTED;
    }

    return AMEDIA_OK;
}

EXPORT
media_status_t AMediaCodecInfo_getEncoderCapabilities(const AMediaCodecInfo *info,
        const ACodecEncoderCapabilities **outEncoderCaps) {
    if (info == nullptr || info->mInfo == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outEncoderCaps = info->mAEncoderCaps.get();

    if ((*outEncoderCaps) == nullptr) {
        return AMEDIA_ERROR_UNSUPPORTED;
    }

    return AMEDIA_OK;
}

// ACodecAudioCapabilities

EXPORT
media_status_t ACodecAudioCapabilities_getBitrateRange(const ACodecAudioCapabilities *audioCaps,
        const AIntRange **outRange) {
    if (audioCaps == nullptr || audioCaps->mBitrateRange == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outRange = audioCaps->mBitrateRange.get();

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecAudioCapabilities_getSupportedSampleRates(
        const ACodecAudioCapabilities *audioCaps, const int **outArrayPtr) {
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

// ACodecPerformancePoint

EXPORT
media_status_t ACodecPerformancePoint_create(int32_t width, int32_t height, int32_t frameRate,
        ACodecPerformancePoint **outPerformancePoint) {
    if (outPerformancePoint == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outPerformancePoint = new ACodecPerformancePoint(
            std::make_shared<VideoCapabilities::PerformancePoint>(width, height, frameRate));

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecPerformancePoint_delete(ACodecPerformancePoint *performancePoint) {
    if (performancePoint == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    delete performancePoint;

    return AMEDIA_OK;
}

EXPORT
bool ACodecPerformancePoint_coversFormat(const ACodecPerformancePoint *performancePoint,
        const AMediaFormat *format) {
    sp<AMessage> nativeFormat;
    AMediaFormat_getFormat(format, &nativeFormat);

    return performancePoint->mPerformancePoint->covers(nativeFormat);
}

EXPORT
bool ACodecPerformancePoint_covers(const ACodecPerformancePoint *one,
        const ACodecPerformancePoint *another) {
    return one->mPerformancePoint->covers(*(another->mPerformancePoint));
}

EXPORT
bool ACodecPerformancePoint_equals(const ACodecPerformancePoint *one,
        const ACodecPerformancePoint *another) {
    return one->mPerformancePoint->equals(*(another->mPerformancePoint));
}

// ACodecVideoCapabilities

EXPORT
media_status_t ACodecVideoCapabilities_getBitrateRange(const ACodecVideoCapabilities *videoCaps,
        const AIntRange **outRange) {
    if (videoCaps == nullptr || videoCaps->mBitrateRange == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outRange = videoCaps->mBitrateRange.get();

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecVideoCapabilities_getSupportedWidths(const ACodecVideoCapabilities *videoCaps,
        const AIntRange **outRange) {
    if (videoCaps == nullptr || videoCaps->mSupportedWidths == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outRange = videoCaps->mSupportedWidths.get();

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecVideoCapabilities_getSupportedHeights(const ACodecVideoCapabilities *videoCaps,
        const AIntRange **outRange) {
    if (videoCaps == nullptr || videoCaps->mSupportedHeights == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outRange = videoCaps->mSupportedHeights.get();

    return AMEDIA_OK;
}

EXPORT
int32_t ACodecVideoCapabilities_getWidthAlignment(const ACodecVideoCapabilities *videoCaps) {
    return videoCaps->mVideoCaps->getWidthAlignment();
}

EXPORT
int32_t ACodecVideoCapabilities_getHeightAlignment(const ACodecVideoCapabilities *videoCaps) {
    return videoCaps->mVideoCaps->getHeightAlignment();
}

EXPORT
int32_t ACodecVideoCapabilities_getSmallerDimensionUpperLimit(const ACodecVideoCapabilities *videoCaps) {
    return videoCaps->mVideoCaps->getSmallerDimensionUpperLimit();
}

EXPORT
media_status_t ACodecVideoCapabilities_getSupportedFrameRates(const ACodecVideoCapabilities *videoCaps,
        const AIntRange **outRange) {
    if (videoCaps == nullptr || videoCaps->mFrameRateRange == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outRange = videoCaps->mFrameRateRange.get();

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecVideoCapabilities_getSupportedWidthsFor(const ACodecVideoCapabilities *videoCaps,
        int32_t height, AIntRange **outRange) {
    if (videoCaps == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    
    std::optional<Range<int32_t>> widthRange = videoCaps->mVideoCaps->getSupportedWidthsFor(height);
    if (!widthRange) {
        return AMEDIA_ERROR_UNSUPPORTED;
    }

    *outRange = new AIntRange(widthRange.value().lower(), widthRange.value().upper());

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecVideoCapabilities_getSupportedHeightsFor(const ACodecVideoCapabilities *videoCaps,
        int32_t width, AIntRange **outRange) {
    if (videoCaps == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    
    std::optional<Range<int32_t>> heightRange = videoCaps->mVideoCaps->getSupportedHeightsFor(width);
    if (!heightRange) {
        return AMEDIA_ERROR_UNSUPPORTED;
    }

    *outRange = new AIntRange(heightRange.value().lower(), heightRange.value().upper());

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecVideoCapabilities_getSupportedFrameRatesFor(const ACodecVideoCapabilities *videoCaps,
        int32_t width, int32_t height, const ADoubleRange **outRange) {
    if (videoCaps == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    std::optional<Range<double>> frameRates
            = videoCaps->mVideoCaps->getSupportedFrameRatesFor(width, height);
    if (!frameRates) {
        return AMEDIA_ERROR_UNSUPPORTED;
    }

    *outRange = new ADoubleRange(frameRates.value().lower(), frameRates.value().upper());

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecVideoCapabilities_getAchievableFrameRatesFor(const ACodecVideoCapabilities *videoCaps,
        int32_t width, int32_t height, const ADoubleRange **outRange) {
    if (videoCaps == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    std::optional<Range<double>> frameRates
            = videoCaps->mVideoCaps->getAchievableFrameRatesFor(width, height);
    if (!frameRates) {
        return AMEDIA_ERROR_UNSUPPORTED;
    }

    *outRange = new ADoubleRange(frameRates.value().lower(), frameRates.value().upper());

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecVideoCapabilities_getSupportedPerformancePoints(
        const ACodecVideoCapabilities *videoCaps, const ACodecPerformancePoint **outPerfromancePointArray) {
    if (videoCaps == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }
    
    *outPerfromancePointArray = videoCaps->mPerformancePoints.data();

    return AMEDIA_OK;
}

EXPORT
bool ACodecVideoCapabilities_areSizeAndRateSupported(const ACodecVideoCapabilities *videoCaps,
        int32_t width, int32_t height, double frameRate) {
    return videoCaps->mVideoCaps->areSizeAndRateSupported(width, height, frameRate);
}

EXPORT
bool ACodecVideoCapabilities_isSizeSupported(const ACodecVideoCapabilities *videoCaps,
        int32_t width, int32_t height) {
    return videoCaps->mVideoCaps->isSizeSupported(width, height);
}

// ACodecEncoderCapabilities

EXPORT
media_status_t ACodecEncoderCapabilities_getQualityRange(const ACodecEncoderCapabilities *encoderCaps,
        const AIntRange  **outRange) {
    if (encoderCaps == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outRange = encoderCaps->mQualityRange.get();

    return AMEDIA_OK;
}

EXPORT
media_status_t ACodecEncoderCapabilities_getComplexityRange(const ACodecEncoderCapabilities *encoderCaps,
        const AIntRange **outRange) {
    if (encoderCaps == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    *outRange = encoderCaps->mComplexityRange.get();

    return AMEDIA_OK;
}

bool ACodecEncoderCapabilities_isBitrateModeSupported(const ACodecEncoderCapabilities *encoderCaps,
        int32_t mode) {
    return encoderCaps->mEncoderCaps->isBitrateModeSupported(mode);
}


}