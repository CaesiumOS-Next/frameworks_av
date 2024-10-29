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

#ifndef _NDK_MEDIA_CODEC_STORE_H
#define _NDK_MEDIA_CODEC_STORE_H

#include <stdint.h>

#include "NdkMediaCodecInfo.h"
#include "NdkMediaError.h"
#include "NdkMediaFormat.h"

__BEGIN_DECLS

/**
 * The media type definition with bitfeids indicating if it is
 * supported by decoders/ encoders/ both.
 */
struct AMediaCodecSupportedMediaType {
    enum Mode : uint32_t {
        FLAG_DECODER = 1 << 0,
        FLAG_ENCODER = 1 << 1,
    };

    // The media type.
    const char *mMediaType;
    // bitfields for modes.
    uint32_t mMode;

    AMediaCodecSupportedMediaType(const char *mediaType, uint32_t mode )
            : mMediaType(mediaType), mMode(mode) {}
};

/**
 * Return the size of supported media types array.
 */
size_t AMediaCodecStore_countSupportedMediaTypes();

/**
 * Get an array of all the supported media types of a device.
 *
 * Return AMEDIA_OK if successfully made the copy.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if the \param outMediaTypes is invalid.
 *
 * Note the AMediaCodecSupportedMediaType structs are owned by the framework.
 * Users are not responsibile for managing the life cycle of them.
 */
media_status_t AMediaCodecStore_getSupportedMediaTypes(
        const AMediaCodecSupportedMediaType **outMediaTypes, size_t max);

/**
 * Get the next decoder info that supports the format.
 *
 * \param outCodecInfo  should be set as nullptr to start the iteration.
 *                      Set the last returned codecInfo to it to get the next supported codecInfo.
 *                      *outCodecInfo will be set to nullptr if reached the end.
 *
 * Return AMEDIA_OK if successfully get the info.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param format is invalid or missing key infos.
 */
media_status_t AMediaCodecStore_findNextDecoderInfoForFormat(
        const AMediaFormat *format, const AMediaCodecInfo **outCodecInfo);

/**
 * Get the next encoder info that supports the format.
 *
 * \param outCodecInfo  should be set as nullptr to start the iteration.
 *                      Set the last returned codecInfo to it to get the next supported codecInfo.
 *                      *outCodecInfo will be set to nullptr if reached the end.
 *
 * Return AMEDIA_OK if successfully get the info.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param format is invalid or missing key infos.
 *
 * No secure encoder will show in the output.
 */
media_status_t AMediaCodecStore_findNextEncoderInfoForFormat(
        const AMediaFormat* format, const AMediaCodecInfo **outCodecInfo);

/**
 * Get the codecInfo corresponding to a given codec name.
 *
 * Return AMEDIA_OK if got the codecInfo successfully.
 * Return AMEDIA_ERROR_UNSUPPORTED if codec not found.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param outCodecInfo is NULL.
 */
media_status_t AMediaCodecStore_getCodecInfo(
        const char *name, const AMediaCodecInfo **outCodecInfo);

__END_DECLS

#endif //_NDK_MEDIA_CODEC_STORE_H