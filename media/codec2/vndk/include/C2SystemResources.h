/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef STAGEFRIGHT_CODEC2_SYSTEM_RESOURCES_H_
#define STAGEFRIGHT_CODEC2_SYSTEM_RESOURCES_H_

#include <C2Config.h>

/**
 *  This is reference implementation of Codec Resources at oem/hal level.
 *
 *  The values (globally available resources) used for each resources should
 *  only be considered as a reference only and the implementation MUST define
 *  these values based on the corresponding system limitations.
 */

/**
 * Resource IDs defined by the software store.
 */
enum C2SoftwareStoreSystemResourceId : uint32_t {
    /// Resource ID for session. The maximum capacity for this is limited at 64.
    SESSION = C2PlatformConfig::resource_id_t::VENDOR_START,
    /// Resource ID for internal memory.
    INTERNAL_MEMORY,
    /// An ID for resources associated with Macroblocks.
    MACROBLOCK_RATE,
    /// An ID for resources associated with Processing Rate.
    PROCESSING_RATE,
};

// max session/instance is capped at 64 in this reference implementation.
const uint64_t kMaxInstanceCount = 64;
// Uses pixel count as a reference for internal memory.
// Since this reference assumes up to 64 instances of 4K codecs
// (As it's a software store, memory is the only limit unlike
// hardware store where codec resources are limited.)
const uint64_t kMaxMemoryInBytes = kMaxInstanceCount * 3840 * 2400;
// This implementation arbitrarily assumes that there are enough macroblocks
// for 64 instances of concurrent codecs at 4K resolution.
// If each macroblock/coding tree unit (CTU) is of size 64x64, that would be:
// 64 * (3840 * 2160) / (64 * 64) = 129600
const uint64_t kMacroBlockSize = 64 * 64;
const uint64_t k4KResolution = 3840 * 2160;
const uint64_t kMaxMacroblocks = kMaxInstanceCount * k4KResolution / kMacroBlockSize;
// This implementation arbitrarily assumes that the implementation can
// concurrently support maximum processing of 64 instances @ 30 frames per second.
const uint64_t kMaxProcessingRate = kMaxInstanceCount * 30;

#endif // STAGEFRIGHT_CODEC2_SYSTEM_RESOURCES_H_
