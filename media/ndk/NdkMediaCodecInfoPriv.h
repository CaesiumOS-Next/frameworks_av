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

#ifndef _NDK_MEDIA_CODEC_INFO_PRIV_H
#define _NDK_MEDIA_CODEC_INFO_PRIV_H

#include <media/NdkMediaCodecInfo.h>

using namespace android;

struct ACodecAudioCapabilities {
    std::shared_ptr<AudioCapabilities> mAudioCaps;

    std::shared_ptr<AIntRange> mBitrateRange;
    std::vector<AIntRange> mSampleRateRanges;
    std::vector<AIntRange> mInputChannelCountRanges;

    void initBitrateRange() {
        const Range<int32_t>& bitrateRange = mAudioCaps->getBitrateRange();
        mBitrateRange = std::make_shared<AIntRange>(bitrateRange.lower(), bitrateRange.upper());
    }

    void initSampleRateRanges() {
        const std::vector<Range<int>>& sampleRateRanges
                = mAudioCaps->getSupportedSampleRateRanges();
        for (auto it = sampleRateRanges.begin(); it != sampleRateRanges.end(); it++) {
            mSampleRateRanges.emplace_back(it->lower(), it->upper());
        }
    }

    void initInputChannelCountRanges() {
        const std::vector<Range<int>>& inputChannels = mAudioCaps->getInputChannelCountRanges();
        for (auto it = inputChannels.begin(); it != inputChannels.end(); it++) {
            mInputChannelCountRanges.emplace_back(it->lower(), it->upper());
        }
    }

    ACodecAudioCapabilities(std::shared_ptr<AudioCapabilities> audioCaps) : mAudioCaps(audioCaps) {
        // mBitrateRange = nullptr;
        // mSampleRateRanges = std::vector<AIntRange>();

        initSampleRateRanges();
        initSampleRateRanges();
        initInputChannelCountRanges();
    }
};

struct AMediaCodecInfo {
    sp<MediaCodecInfo> mInfo;
    const char* mMediaType;
    std::shared_ptr<CodecCapabilities> mCodecCaps;

    std::shared_ptr<const ACodecAudioCapabilities> mAAudioCaps;

    AMediaCodecInfo(sp<MediaCodecInfo> info) : mInfo(info) {
        Vector<AString> mediaTypes;
        mInfo->getSupportedMediaTypes(&mediaTypes);
        mMediaType = mediaTypes.top().c_str();
        mCodecCaps = mInfo->getCodecCapsFor(mMediaType);
        mAAudioCaps = std::make_shared<const ACodecAudioCapabilities>(
                mCodecCaps->getAudioCapabilities());
    }
};

#endif //_NDK_MEDIA_CODEC_INFO_PRIV_H