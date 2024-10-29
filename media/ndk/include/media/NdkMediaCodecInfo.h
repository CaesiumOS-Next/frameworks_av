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
struct ACodecPerformancePoint;
struct ACodecVideoCapabilities;
struct ACodecEncoderCapabilities;
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

/**
 * Deleter of AIntRange.
 *
 * \return AMEDIA_OK if deleted the AIntRange successfully.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param range is invalid.
 */
media_status_t AIntRange_delete(AIntRange *range);

/**
 * Deleter of ADoubleRange.
 *
 * \return AMEDIA_OK if deleted the ADoubleRange successfully.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param range is invalid.
 */
media_status_t ADoubleRange_delete(ADoubleRange *range);

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
 * Return AMEDIA_ERROR_UNSUPPORTED if the codec is not an audio codec.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param info is invalid.
 */
media_status_t AMediaCodecInfo_getAudioCapabilities(const AMediaCodecInfo *info,
        const ACodecAudioCapabilities **outAudioCaps);

/**
 * Get the ACodecVideoCapabilities from the given AMediaCodecInfo.
 *
 * Return AMEDIA_OK if successfully got the ACodecVideoCapabilities.
 * Return AMEDIA_ERROR_UNSUPPORTED if the codec is not a video codec.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param info is invalid.
 */
media_status_t AMediaCodecInfo_getVideoCapabilities(const AMediaCodecInfo *info,
        const ACodecVideoCapabilities **outVideoCaps);

/**
 * Get the ACodecEncoderCapabilities from the given AMediaCodecInfo.
 *
 * Return AMEDIA_OK if successfully got the ACodecEncoderCapabilities.
 * Return AMEDIA_ERROR_UNSUPPORTED if the codec is not an encoder.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param info is invalid.
 */
media_status_t AMediaCodecInfo_getEncoderCapabilities(const AMediaCodecInfo *info,
        const ACodecEncoderCapabilities **outEncoderCaps);

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

// ACodecPerformancePoint

/**
 * Create a performance point for a given frame size and frame rate.
 *
 * \param width width of the frame in pixels
 * \param height height of the frame in pixels
 * \param frameRate frame rate in frames per second
 *
 * Return AMEDIA_OK if a performance point is successfully created.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param outPerformancePoint is invalid.
 */
media_status_t ACodecPerformancePoint_create(int32_t width, int32_t height, int32_t frameRate,
        ACodecPerformancePoint **outPerformancePoint);

/**
 * Delete a created performance point.
 *
 * Return AMEDIA_OK if it is successfully deleted.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param performancePoint is invalid.
 */
media_status_t ACodecPerformancePoint_delete(ACodecPerformancePoint *performancePoint);

/**
 * Checks whether the performance point covers a media format.
 *
 * \param format Stream format considered.
 * \return true if the performance point covers the format.
 */
bool ACodecPerformancePoint_coversFormat(const ACodecPerformancePoint *performancePoint,
        const AMediaFormat *format);

/**
 * Checks whether a performance point covers another performance point. Use this
 * method to determine if a performance point advertised by a codec covers the
 * performance point required. This method can also be used for loose ordering as this
 * method is transitive.
 *
 * \return true if \param one covers \param another.
 */
bool ACodecPerformancePoint_covers(const ACodecPerformancePoint *one,
        const ACodecPerformancePoint *another);

/**
 * Checks whether two performane points are equal.
 */
bool ACodecPerformancePoint_equals(const ACodecPerformancePoint *one,
        const ACodecPerformancePoint *another);

// ACodecVideoCapabilities

/**
 * Get the range of supported bitrates in bits/second.
 *
 * \return AMEDIA_OK if got the supported bitrates successfully.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param videoCaps is invalid.
 */
media_status_t ACodecVideoCapabilities_getBitrateRange(const ACodecVideoCapabilities *videoCaps,
        const AIntRange **outRange);

/**
 * Get the range of supported video widths.
 *
 * \return AMEDIA_OK if got the supported video widths successfully.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param videoCaps is invalid.
 */
media_status_t ACodecVideoCapabilities_getSupportedWidths(const ACodecVideoCapabilities *videoCaps,
        const AIntRange **outRange);

/**
 * Get the range of supported video heights.
 *
 * \return AMEDIA_OK if got the supported video heights successfully.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param videoCaps is invalid.
 */
media_status_t ACodecVideoCapabilities_getSupportedHeights(const ACodecVideoCapabilities *videoCaps,
        const AIntRange **outRange);

/**
 * \return the alignment requirement for video width (in pixels).
 *
 * This is a power-of-2 value that video width must be a multiple of.
 */
int32_t ACodecVideoCapabilities_getWidthAlignment(const ACodecVideoCapabilities *videoCaps);

/**
 * \return the alignment requirement for video height (in pixels).
 *
 * This is a power-of-2 value that video height must be a multiple of.
 */
int32_t ACodecVideoCapabilities_getHeightAlignment(const ACodecVideoCapabilities *videoCaps);

/**
 * \return the upper limit on the smaller dimension of width or height.
 */
int32_t ACodecVideoCapabilities_getSmallerDimensionUpperLimit(
        const ACodecVideoCapabilities *videoCaps);

/**
 * Get the range of supported frame rates.
 *
 * This is not a performance indicator. Rather, it expresses the limits specified in the coding
 * standard, based on the complexities of encoding material for later playback at a certain
 * frame rate, or the decoding of such material in non-realtime.
 *
 * \return AMEDIA_OK if got the frame rate range successfully.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param videoCaps is invalid.
 */
media_status_t ACodecVideoCapabilities_getSupportedFrameRates(
        const ACodecVideoCapabilities *videoCaps, const AIntRange **outRange);

/**
 * Get the range of supported video widths for a video height.
 *
 * This function will allocate a memory for AIntRange.
 * Users are responsible for calling 'AIntRange_delete(*outRange)' to release the memory.
 *
 * \return AMEDIA_OK if got the supported video width range successfully.
 * \return AMEDIA_ERROR_UNSUPPORTED if the height is not supported.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param videoCaps is invalid.
 */
media_status_t ACodecVideoCapabilities_getSupportedWidthsFor(
        const ACodecVideoCapabilities *videoCaps, int32_t height, AIntRange **outRange);

/**
 * Get the range of supported video heights for a video width.
 *
 * This function will allocate a memory for AIntRange.
 * Users are responsible for calling 'AIntRange_delete(*outRange)' to release the memory.
 *
 * \return AMEDIA_OK if got the supported video height range successfully.
 * \return AMEDIA_ERROR_UNSUPPORTED if the width is not supported.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param videoCaps is invalid.
 */
media_status_t ACodecVideoCapabilities_getSupportedHeightsFor(
        const ACodecVideoCapabilities *videoCaps, int32_t width, AIntRange **outRange);

/**
 * Get the range of supported video frame rates for a video size.
 *
 * This is not a performance indicator.  Rather, it expresses the limits specified in the coding
 * standard, based on the complexities of encoding material of a given size for later playback at
 * a certain frame rate, or the decoding of such material in non-realtime.
 *
 * This function will allocate a memory for AIntRange.
 * Users are responsible for calling 'ADoubleRange_delete(*outRange)' to release the memory.
 *
 * \return AMEDIA_OK if got the supported video frame rates successfully.
 * \return AMEDIA_ERROR_UNSUPPORTED if the size is not supported.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param videoCaps is invalid.
 */
media_status_t ACodecVideoCapabilities_getSupportedFrameRatesFor(
        const ACodecVideoCapabilities *videoCaps, int32_t width, int32_t height,
        const ADoubleRange **outRange);

/**
 * Get the range of achievable video frame rates for a video size.
 *
 * This function will allocate a memory for AIntRange.
 * Users are responsible for calling 'ADoubleRange_delete(*outRange)' to release the memory.
 *
 * \return AMEDIA_OK if got the achievable video frame rates successfully.
 * \return AMEDIA_ERROR_UNSUPPORTED if the codec did not publish any measurement data.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param videoCaps is invalid.
 */
media_status_t ACodecVideoCapabilities_getAchievableFrameRatesFor(
        const ACodecVideoCapabilities *videoCaps, int32_t width, int32_t height,
        const ADoubleRange **outRange);

/**
 * Get the supported performance points.
 *
 * \return AMEDIA_OK if successfully got the performance points.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param videoCaps is invalid.
 */
media_status_t ACodecVideoCapabilities_getSupportedPerformancePoints(
        const ACodecVideoCapabilities *videoCaps,
        const ACodecPerformancePoint **outPerformancePointArray);

/**
 * \return whether a givend video size and frameRate combination is supported.
 */
bool ACodecVideoCapabilities_areSizeAndRateSupported(const ACodecVideoCapabilities *videoCaps,
        int32_t width, int32_t height, double frameRate);

/**
 * \return whether a given video size is supported.
 */
bool ACodecVideoCapabilities_isSizeSupported(const ACodecVideoCapabilities *videoCaps,
        int32_t width, int32_t height);

// ACodecEncoderCapabilities

/**
 * Get the supported range of quality values.
 *
 * Quality is implementation-specific. As a general rule, a higher quality
 * setting results in a better image quality and a lower compression ratio.
 *
 * \return AMEDIA_OK if successfully got the quality range.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param videoCaps is invalid.
 */
media_status_t ACodecEncoderCapabilities_getQualityRange(
        const ACodecEncoderCapabilities *encoderCaps, const AIntRange  **outRange);

/**
 * Get the supported range of encoder complexity values.
 *
 * Some codecs may support multiple complexity levels, where higher complexity values use more
 * encoder tools (e.g. perform more intensive calculations) to improve the quality or the
 * compression ratio. Use a lower value to save power and/or time.
 *
 * \return AMEDIA_OK if successfully got the complexity range.
 * \return AMEDIA_ERROR_INVALID_PARAMETER if \param videoCaps is invalid.
 */
media_status_t ACodecEncoderCapabilities_getComplexityRange(
        const ACodecEncoderCapabilities *encoderCaps, const AIntRange **outRange);

/**
 * Query whether a bitrate mode is supported.
 */
bool ACodecEncoderCapabilities_isBitrateModeSupported(const ACodecEncoderCapabilities *encoderCaps,
        int32_t mode);

__END_DECLS

#endif //_NDK_MEDIA_CODEC_INFO_H