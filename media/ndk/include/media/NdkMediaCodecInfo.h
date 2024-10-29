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

#ifndef _NDK_MEDIA_CODEC_INFO_H
#define _NDK_MEDIA_CODEC_INFO_H

#include "NdkMediaError.h"
#include "NdkMediaFormat.h"

#include <media/MediaCodecInfo.h>

__BEGIN_DECLS

struct ACodecAudioCapabilities;
struct AMediaCodecInfo;

/**
 * The uitl struct describing the range of two integer values.
 */
struct AIntRange {
    int32_t mLower;
    int32_t mUpper;

    AIntRange(int32_t lower, int32_t upper) : mLower(lower), mUpper(upper) {}
};

/**
 * The uitl struct describing the range of two double values.
 */
struct ADoubleRange {
    double mLower;
    double mUpper;

    ADoubleRange(double lower, double upper) : mLower(lower), mUpper(upper) {}
};

// AMediaCodecInfo

/**
 * Get the canonical name of a codec.
 *
 * Return NULL if \param info is invalid.
 */
const char* AMediaCodecInfo_getCanonicalName(const AMediaCodecInfo *info);

/**
 * Query if the codec is an encoder.
 */
bool AMediaCodecInfo_isEncoder(const AMediaCodecInfo *info);

/**
 * Query if the codec is provided by the Android platform (false) or the device manufacturer (true).
 */
bool AMediaCodecInfo_isVendor(const AMediaCodecInfo *info);

/**
 * The type of codecs.
 */
enum AMediaCodecType : int32_t {
    /**
     * It is a software codec.
     *
     * Software-only codecs are more secure as they run in a tighter security sandbox.
     * On the other hand, software-only codecs do not provide any performance guarantees.
     */
    SOFTWARE_ONLY = 1,

    /**
     * It is a hardware codec.
     *
     * This attribute is provided by the device manufacturer.
     * Note that it cannot be tested for correctness.
     */
    HARDWARE_ACCELERATED = 2,

    /**
     * Unknown if it is a software or hardware codec.
     */
    UNKNOWN = 3,
};

/**
 * Query if the codec is software-only, hardware-accelerated or hybrid.
 *
 * Return 0 if \param info is invalid.
 */
AMediaCodecType AMediaCodecInfo_getMediaCodecInfoType(const AMediaCodecInfo *info);

/**
 * Get the supported media type of the codec.
 *
 * Return AMEDIA_OK if successfully got the media type.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param info is invalid.
 */
media_status_t AMediaCodecInfo_getMediaType(const AMediaCodecInfo *info, const char **outMediaType);

/**
 * Returns the max number of the supported concurrent codec instances.
 *
 * This is a hint for an upper bound. Applications should not expect to successfully
 * operate more instances than the returned value, but the actual number of
 * concurrently operable instances may be less as it depends on the available
 * resources at time of use.
 */
int32_t AMediaCodecInfo_getMaxSupportedInstances(const AMediaCodecInfo *info);

/**
 * Query codec feature capabilities.
 *
 * These features are supported to be used by the codec.  These
 * include optional features that can be turned on, as well as
 * features that are always on.
 */
bool AMediaCodecInfo_isFeatureSupported(const AMediaCodecInfo *info, const char *featureName);

/**
 * Query codec feature requirements.
 *
 * These features are required to be used by the codec, and as such,
 * they are always turned on.
 */
bool AMediaCodecInfo_isFeatureRequired(const AMediaCodecInfo *info, const char *featureName);

/**
 * Query whether codec supports a given \param format.
 */
bool AMediaCodecInfo_isFormatSupported(const AMediaCodecInfo *info, const AMediaFormat *format);

/**
 * Get the ACodecAudioCapabilities from the given AMediaCodecInfo.
 *
 * Return AMEDIA_OK if successfully got the ACodecAudioCapabilities.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param info is invalid.
 */
media_status_t AMediaCodecInfo_getAudioCapabilities(const AMediaCodecInfo *info,
        const ACodecAudioCapabilities **outAudioCaps);

// ACodecAudioCapabilities

/**
 * Get the range of supported bitrates in bits/second.
 *
 * Return AMEDIA_OK if got bitrates successfully.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param audioCaps is invalid.
 */
media_status_t ACodecAudioCapabilities_getBitrateRange(const ACodecAudioCapabilities *audioCaps,
        const AIntRange **outRange);

/**
 * Get the array of supported sample rates
 * and return AMEDIA_OK if the codec supports only discrete values.
 * Otherwise, it returns AMEDIA_ERROR_UNSUPPORTED.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param audioCaps is invalid.
 * The array is sorted in ascending order.
 */
media_status_t ACodecAudioCapabilities_getSupportedSampleRates(
        const ACodecAudioCapabilities *audioCaps, const int **outArrayPtr);

/**
 * Get the array of supported sample rate ranges.
 * The array is sorted in ascending order, and the ranges are distinct.
 *
 * Return AMEDIA_OK if got the sample rate ranges successfully.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param audioCaps is invalid.
 */
media_status_t ACodecAudioCapabilities_getSupportedSampleRateRanges(
        const ACodecAudioCapabilities *audioCaps, const AIntRange **outArrayPtr);

/**
 * Return the maximum number of input channels supported.
 */
int32_t ACodecAudioCapabilities_getMaxInputChannelCount(const ACodecAudioCapabilities *audioCaps);

/**
 * Returns the minimum number of input channels supported.
 * This is often 1, but does vary for certain mime types.
 */
int32_t ACodecAudioCapabilities_getMinInputChannelCount(const ACodecAudioCapabilities *audioCaps);

/**
 * Get an array of ranges representing the number of input channels supported.
 * The codec supports any number of input channels within this range.
 * For many codecs, this will be a single range [1..N], for some N.
 *
 * Return AMEDIA_OK if got the input channel array successfully.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param audioCaps is invalid.
 */
media_status_t ACodecAudioCapabilities_getInputChannelCountRanges(
        const ACodecAudioCapabilities *audioCaps, const AIntRange **outArrayPtr);

/**
 * Query whether the sample rate is supported by the codec.
 */
bool ACodecAudioCapabilities_isSampleRateSupported(const ACodecAudioCapabilities *audioCaps,
        int32_t sampleRate);

__END_DECLS

#endif //_NDK_MEDIA_CODEC_INFO_H