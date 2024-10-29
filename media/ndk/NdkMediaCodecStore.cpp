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

#include <media/NdkMediaFormatPriv.h>
#include <media/NdkMediaCodecStore.h>

#include <media/IMediaCodecList.h>

#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaCodecConstants.h>
#include <media/stagefright/MediaCodecList.h>

using namespace android;

static sp<IMediaCodecList> sCodecList = MediaCodecList::getLocalInstance();

static std::map<std::string, const AMediaCodecSupportedMediaType> sMediaTypes;

static std::vector<std::shared_ptr<const AMediaCodecInfo>> sEncoderInfos;
static std::vector<std::shared_ptr<const AMediaCodecInfo>> sDecoderInfos;

static std::vector<std::shared_ptr<const AMediaCodecInfo>>::iterator sEncoderInfosIt;
static std::vector<std::shared_ptr<const AMediaCodecInfo>>::iterator sDecoderInfosIt;

static media_status_t initInfoList(const AMediaFormat *format, bool isEncoder) {
    if (format == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    sp<AMessage> nativeFormat;
    AMediaFormat_getFormat(format, &nativeFormat);

    AString mediaType;
    if (!nativeFormat->findString(KEY_MIME, &mediaType)) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    if (isEncoder) {
        sEncoderInfos.clear();
    } else {
        sDecoderInfos.clear();
    }

    size_t index = 0;
    for (;;) {
        ssize_t matchIndex = sCodecList->findCodecByType(mediaType.c_str(), isEncoder, index);
        if (matchIndex < 0) {
            break;
        }

        index = matchIndex + 1;

        const sp<MediaCodecInfo> info = sCodecList->getCodecInfo(matchIndex);
        CHECK(info != nullptr);

        if (!MediaCodecList::codecHandlesFormat(mediaType.c_str(), info, nativeFormat)) {
            continue;
        }

        if (isEncoder) {
            sEncoderInfos.push_back(std::make_shared<AMediaCodecInfo>(info));
        } else {
            sDecoderInfos.push_back(std::make_shared<AMediaCodecInfo>(info));
        }
    }

    if (isEncoder) {
        sEncoderInfosIt = sEncoderInfos.begin();
    } else {
        sDecoderInfosIt = sDecoderInfos.begin();
    }

    return AMEDIA_OK;
}

extern "C" {

EXPORT
size_t AMediaCodecStore_countSupportedMediaTypes() {
    if (sMediaTypes.size() != 0) {
        return sMediaTypes.size();
    }

    for (size_t idx = 0; idx < sCodecList->countCodecs(); idx++) {
        sp<MediaCodecInfo> codecInfo = sCodecList->getCodecInfo(idx);
        Vector<AString> codecMediaTypes;
        codecInfo->getSupportedMediaTypes(&codecMediaTypes);
        for (AString codecMediaType : codecMediaTypes) {
            std::string mediaType = std::string(codecMediaType.c_str());
            auto it = sMediaTypes.find(mediaType);
            uint32_t mode = (it == sMediaTypes.end() ? 0 : it->second.mMode);
            mode |= (codecInfo->isEncoder() ? AMediaCodecSupportedMediaType::FLAG_ENCODER
                                            : AMediaCodecSupportedMediaType::FLAG_DECODER);
            if (it != sMediaTypes.end()) {
                sMediaTypes.erase(it);
            }

            sMediaTypes.emplace(mediaType, AMediaCodecSupportedMediaType(mediaType.c_str(), mode));
        }
    }

    return sMediaTypes.size();
}

EXPORT
media_status_t AMediaCodecStore_getSupportedMediaTypes(
        const AMediaCodecSupportedMediaType **outMediaTypes, size_t max) {
    size_t count = AMediaCodecStore_countSupportedMediaTypes();
    if (outMediaTypes == NULL) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    auto it = sMediaTypes.begin();
    for (int i = 0; i < max && it != sMediaTypes.end(); i++) {
        outMediaTypes[i] = &(it->second);
        it++;
    }

    return AMEDIA_OK;
}

EXPORT
media_status_t AMediaCodecStore_findNextDecoderInfoForFormat(
        const AMediaFormat *format, const AMediaCodecInfo **outCodecInfo){
    if (outCodecInfo == nullptr) {  // reset the codecInfo list with a new supported format
        if (initInfoList(format, false) != AMEDIA_OK) {
            return AMEDIA_ERROR_INVALID_PARAMETER;
        }
    }

    if (sDecoderInfosIt == sDecoderInfos.end()) {  // reaching the end
        *outCodecInfo = nullptr;
    } else {
        *outCodecInfo = (*sDecoderInfosIt).get();
        sDecoderInfosIt++;
    }

    return AMEDIA_OK;
}

EXPORT
media_status_t AMediaCodecStore_findNextEncoderInfoForFormat(
        const AMediaFormat *format, const AMediaCodecInfo **outCodecInfo){
    if (outCodecInfo == nullptr) {  // reset the codecInfo list with a new supported format
        if (initInfoList(format, true) != AMEDIA_OK) {
            return AMEDIA_ERROR_INVALID_PARAMETER;
        }
    }

    if (sEncoderInfosIt == sEncoderInfos.end()) {  // reaching the end
        *outCodecInfo = nullptr;
    } else {
        *outCodecInfo = (*sEncoderInfosIt).get();
        sEncoderInfosIt++;
    }

    return AMEDIA_OK;
}

EXPORT
media_status_t AMediaCodecStore_getCodecInfo(
        const char *name, const AMediaCodecInfo **outCodecInfo) {
    if (outCodecInfo == nullptr) {
        return AMEDIA_ERROR_INVALID_PARAMETER;
    }

    for (auto it = sDecoderInfos.begin(); it != sDecoderInfos.end(); it++) {
        if (strcmp((*it)->mInfo->getCodecName(), name) == 0) {
            *outCodecInfo = (*it).get();
            return AMEDIA_OK;
        }
    }
    for (auto it = sEncoderInfos.begin(); it != sEncoderInfos.end(); it++) {
        if (strcmp((*it)->mInfo->getCodecName(), name) == 0) {
            *outCodecInfo = (*it).get();
            return AMEDIA_OK;
        }
    }

    *outCodecInfo = nullptr;
    return AMEDIA_ERROR_UNSUPPORTED;
}

}