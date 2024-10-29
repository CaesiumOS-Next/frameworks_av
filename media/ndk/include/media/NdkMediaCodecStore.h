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
typedef struct AMediaCodecSupportedMediaType {
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
} AMediaCodecSupportedMediaType;

/**
 * Get an array of all the supported media types of a device.
 *
 * \param outMediaTypes The pointer to the output AMediaCodecSupportedMediaType array.
 *                      It is owned by the fraework and has an infinite lifecycle.
 *
 * \param outCount size of the out array.
 *
 * Return AMEDIA_OK if successfully made the copy.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if the \param outMediaTypes is invalid.
 */
media_status_t AMediaCodecStore_getSupportedMediaTypes(
        const AMediaCodecSupportedMediaType **outMediaTypes, size_t *outCount) __INTRODUCED_IN(36);

/**
 * Get the next decoder info that supports the format.
 *
 * \param outCodecInfo  should be set as NULL to start the iteration.
 *                      Set the last codecInfo you got to it to get the next one.
 *                      *outCodecInfo would be set to NULL if reached the end.
 *                      It is owned by the framework and has an infinite lifecycle.
 *
 * \param format        If set as NULL, this API will iterate through all available decoders.
 *                      If Not NULL, the format MUST contain key "mime" implying the media type.
 *
 * Return AMEDIA_OK if successfully got the info.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param outCodecInfo is invalid.
 * Return AMEDIA_ERROR_UNSUPPORTED if no more decoder supporting the format.
 *
 * It is undefined behavior to call this API with a NON NULL \param outCodecInfo
 * and a different \param format during an iteration.
 * The API can only be reset when user call with NULL \param outCodecInfo.
 */
media_status_t AMediaCodecStore_findNextDecoderForFormat(
        const AMediaFormat *format, const AMediaCodecInfo **outCodecInfo) __INTRODUCED_IN(36);

/**
 * Get the next encoder info that supports the format.
 *
 * \param outCodecInfo  should be set as NULL to start the iteration.
 *                      Set the last codecInfo you got to it to get the next one.
 *                      *outCodecInfo would be set to NULL if reached the end.
 *                      It is owned by the framework and has an infinite lifecycle.
 *
 * \param format        If set as NULL, this API will iterate through all available encoders.
 *                      If Not NULL, the format MUST contain key "mime" implying the media type.
 *
 * Return AMEDIA_OK if successfully got the info.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param outCodecInfo is invalid.
 * Return AMEDIA_ERROR_UNSUPPORTED if no more encoder supporting the format.
 *
 * It is undefined behavior to call this API with a NON NULL \param outCodecInfo
 * and a different \param format during an iteration.
 * The API can only be reset when user call with NULL \param outCodecInfo.
 *
 * No secure encoder will show in the output.
 */
media_status_t AMediaCodecStore_findNextEncoderForFormat(
        const AMediaFormat* format, const AMediaCodecInfo **outCodecInfo) __INTRODUCED_IN(36);

/**
 * Get the codecInfo corresponding to a given codec name.
 *
 * \param name          Media type name.
 *                      Users might get valid media types names from the mMediaTypes field
 *                      of AMediaCodecSupportedMediaType structs.
 *
 * \param outCodecInfo  Output parameter for the corresponding AMeidaCodecInfo struct.
 *                      It is owned by the framework and has an infinite lifecycle.
 *
 * Return AMEDIA_OK if got the codecInfo successfully.
 * Return AMEDIA_ERROR_UNSUPPORTED if no corresponding codec found.
 * Return AMEDIA_ERROR_INVALID_PARAMETER if \param outCodecInfo is invalid.
 */
media_status_t AMediaCodecStore_getCodecInfo(
        const char *name, const AMediaCodecInfo **outCodecInfo) __INTRODUCED_IN(36);

__END_DECLS

#endif //_NDK_MEDIA_CODEC_STORE_H