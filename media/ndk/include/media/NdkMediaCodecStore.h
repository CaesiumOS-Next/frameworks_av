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
        kFlagDecoder = 1 << 0,
        kFlagEncoder = 1 << 1,
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
 * Return an array of all the supported media types of a device.
 */
media_status_t AMediaCodecStore_getSupportedMediaTypes(
        AMediaCodecSupportedMediaType **outMediaTypes, size_t max);

/**
 * Return the next decoder info that supports the format.
 *
 * \param outCodecInfo should be set as nullptr to start the iteration.
 * Set the last codecInfo to it to get the next supported codecInfo.
 *
 * Return nullptr if reached the end.
 */
media_status_t AMediaCodecStore_findNextDecoderInfoForFormat(
        const AMediaFormat *format, AMediaCodecInfo **outCodecInfo);

/**
 * Return the next encoder info that supports the format.
 *
 * \param outCodecInfo  should be set as nullptr to start the iteration.
 *                      Set the last returned codecInfo to it to get the next supported codecInfo.
 *                      Nullptr will be returned if reached the end.
 *
 * This API is not returning any secure encoders.
 */
media_status_t AMediaCodecStore_findNextEncoderInfoForFormat(
        const AMediaFormat* format, AMediaCodecInfo **outCodecInfo);

/**
 * Return the codecInfo corresponding to the given codec name.
 */
media_status_t AMediaCodecStore_getCodecInfoByName(
        const char *name, AMediaCodecInfo **outCodecInfo);

__END_DECLS

#endif //_NDK_MEDIA_CODEC_STORE_H