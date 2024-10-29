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
#define LOG_TAG "NdkMediaCodecStore"

#include "NdkMediaCodecInfoPriv.h"

#include <media/NdkMediaCodecStore.h>
#include <media/NdkMediaFormatPriv.h>

#include <media/IMediaCodecList.h>

#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaCodecConstants.h>
#include <media/stagefright/MediaCodecList.h>

using namespace android;

static sp<IMediaCodecList> sCodecList = MediaCodecList::getLocalInstance();
static std::vector<AMediaCodecSupportedMediaType> sMediaTypes;
static std::shared_ptr<AMediaCodecInfo> sDummyHead;

static void initMediaTypes() {
    static std::map<std::string, const AMediaCodecSupportedMediaType> typesInfoMap;
    for (size_t idx = 0; idx < sCodecList->countCodecs(); idx++) {
        sp<MediaCodecInfo> codecInfo = sCodecList->getCodecInfo(idx);
        Vector<AString> codecMediaTypes;
        codecInfo->getSupportedMediaTypes(&codecMediaTypes);
        for (AString codecMediaType : codecMediaTypes) {
            std::string mediaType = std::string(codecMediaType.c_str());
            auto it = typesInfoMap.find(mediaType);
            uint32_t mode = (it == typesInfoMap.end() ? 0 : it->second.mMode);
            mode |= (codecInfo->isEncoder() ? AMediaCodecSupportedMediaType::FLAG_ENCODER
                                            : AMediaCodecSupportedMediaType::FLAG_DECODER);
            if (it != typesInfoMap.end()) {
                typesInfoMap.erase(it);
            }
            typesInfoMap.emplace(mediaType, AMediaCodecSupportedMediaType(mediaType.c_str(), mode));
        }
    }

    for (auto it = typesInfoMap.begin(); it != typesInfoMap.end(); it++) {
        sMediaTypes.push_back(it->second);
    }
}

static void buildLinkedList() {
    sDummyHead = std::make_shared<AMediaCodecInfo>(nullptr);
    std::shared_ptr<AMediaCodecInfo> it = sDummyHead;
    for (size_t i = 0; i < sCodecList->countCodecs(); i++) {
        sp<MediaCodecInfo> info = sCodecList->getCodecInfo(i);
        std::shared_ptr<AMediaCodecInfo> node = std::make_shared<AMediaCodecInfo>(info);
        it->next = node;
        it = it->next;
    }
}

static bool codecHandlesFormat(const AMediaFormat *format, bool isEncoder,
        const AMediaCodecInfo *const codecInfo) {
    sp<AMessage> nativeFormat;
    AMediaFormat_getFormat(format, &nativeFormat);
    return codecInfo->mCodecCaps->isEncoder() == isEncoder
            && codecInfo->mCodecCaps->isFormatSupported(nativeFormat);
}

static media_status_t findNextCodecForFormat(
        const AMediaFormat *format, bool isEncoder, const AMediaCodecInfo **outCodecInfo) {
    if (outCodecInfo == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    if (sDummyHead == nullptr) {
        buildLinkedList();
    }

    if (*outCodecInfo == nullptr) {  // reset to the header
        *outCodecInfo = sDummyHead.get();
    }

    do {
        *outCodecInfo = (*outCodecInfo)->next.get();
    } while (*outCodecInfo != nullptr && !codecHandlesFormat(format, isEncoder, *outCodecInfo));

    return *outCodecInfo == nullptr ? AMEDIA_ERROR_UNSUPPORTED : AMEDIA_OK;
}

extern "C" {

EXPORT
media_status_t AMediaCodecStore_getSupportedMediaTypes(
        const AMediaCodecSupportedMediaType **outMediaTypes, size_t *outCount) {
    if (outMediaTypes == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    if (sMediaTypes.empty()) {
        initMediaTypes();
    }

    *outCount = sMediaTypes.size();
    *outMediaTypes = sMediaTypes.data();

    return AMEDIA_OK;
}

EXPORT
media_status_t AMediaCodecStore_findNextDecoderForFormat(
        const AMediaFormat *format, const AMediaCodecInfo **outCodecInfo){
    return findNextCodecForFormat(format, false, outCodecInfo);
}

EXPORT
media_status_t AMediaCodecStore_findNextEncoderForFormat(
        const AMediaFormat *format, const AMediaCodecInfo **outCodecInfo){
    return findNextCodecForFormat(format, true, outCodecInfo);
}

EXPORT
media_status_t AMediaCodecStore_getCodecInfo(
        const char *name, const AMediaCodecInfo **outCodecInfo) {
    if (outCodecInfo == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    std::shared_ptr<AMediaCodecInfo> it = sDummyHead;
    while (it->next != nullptr) {
        it = it->next;
        if (strcmp(it->mMediaType, name) == 0) {
            *outCodecInfo = it.get();
            return AMEDIA_OK;
        }
    }

    *outCodecInfo = nullptr;
    return AMEDIA_ERROR_UNSUPPORTED;
}

}