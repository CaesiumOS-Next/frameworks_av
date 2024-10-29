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
        initBitrateRange();
        initSampleRateRanges();
        initInputChannelCountRanges();
    }
};

struct ACodecPerformancePoint {
    std::shared_ptr<const VideoCapabilities::PerformancePoint> mPerformancePoint;

    ACodecPerformancePoint(std::shared_ptr<const VideoCapabilities::PerformancePoint> performancePoint)
            : mPerformancePoint(performancePoint) {}
};

struct ACodecVideoCapabilities {
    std::shared_ptr<VideoCapabilities> mVideoCaps;

    std::shared_ptr<AIntRange> mBitrateRange;
    std::shared_ptr<AIntRange> mSupportedWidths;
    std::shared_ptr<AIntRange> mSupportedHeights;
    std::shared_ptr<AIntRange> mFrameRateRange;
    std::vector<ACodecPerformancePoint> mPerformancePoints;

    void initBitrateRange() {
        const Range<int32_t>& bitrateRange = mVideoCaps->getBitrateRange();
        mBitrateRange = std::make_shared<AIntRange>(bitrateRange.lower(), bitrateRange.upper());
    }

    void initSupportedWidths() {
        const Range<int32_t>& supportedWidths = mVideoCaps->getSupportedWidths();
        mSupportedWidths = std::make_shared<AIntRange>(supportedWidths.lower(), supportedWidths.upper());
    }

    void initSupportedHeights() {
        const Range<int32_t>& supportedHeights = mVideoCaps->getSupportedHeights();
        mSupportedHeights = std::make_shared<AIntRange>(supportedHeights.lower(), supportedHeights.upper());
    }

    void initFrameRateRange() {
        const Range<int32_t>& frameRateRange = mVideoCaps->getSupportedFrameRates();
        mFrameRateRange = std::make_shared<AIntRange>(frameRateRange.lower(), frameRateRange.upper());
    }

    void initPerformancePoints() {
        const std::vector<VideoCapabilities::PerformancePoint>& performancePoints
            = mVideoCaps->getSupportedPerformancePoints();
        for (auto it = performancePoints.begin(); it != performancePoints.end(); it++) {
            mPerformancePoints.emplace_back(
                    std::shared_ptr<const VideoCapabilities::PerformancePoint>(&(*it)));
        }
    }

    ACodecVideoCapabilities(std::shared_ptr<VideoCapabilities> videoCaps) : mVideoCaps(videoCaps) {
        initBitrateRange();
        initSupportedWidths();
        initSupportedHeights();
        initFrameRateRange();
        initPerformancePoints();
    }
};

struct ACodecEncoderCapabilities {
    std::shared_ptr<EncoderCapabilities> mEncoderCaps;

    std::shared_ptr<AIntRange> mQualityRange;
    std::shared_ptr<AIntRange> mComplexityRange;

    void initQualityRange() {
        const Range<int32_t>& qualityRange = mEncoderCaps->getQualityRange();
        mQualityRange = std::make_shared<AIntRange>(qualityRange.lower(), qualityRange.upper());
    }

    void initComplexityRange() {
        const Range<int32_t>& complexityRange = mEncoderCaps->getComplexityRange();
        mComplexityRange = std::make_shared<AIntRange>(complexityRange.lower(), complexityRange.upper());
    }

    ACodecEncoderCapabilities(std::shared_ptr<EncoderCapabilities> encoderCaps)
            : mEncoderCaps(encoderCaps) {
        initQualityRange();
        initComplexityRange();
    }
};

struct AMediaCodecInfo {
    sp<MediaCodecInfo> mInfo;
    const char* mMediaType;
    std::shared_ptr<CodecCapabilities> mCodecCaps;

    std::shared_ptr<const ACodecAudioCapabilities> mAAudioCaps;
    std::shared_ptr<const ACodecVideoCapabilities> mAVideoCaps;
    std::shared_ptr<const ACodecEncoderCapabilities> mAEncoderCaps;

    AMediaCodecInfo(sp<MediaCodecInfo> info) : mInfo(info) {
        Vector<AString> mediaTypes;
        mInfo->getSupportedMediaTypes(&mediaTypes);
        mMediaType = mediaTypes.top().c_str();
        mCodecCaps = mInfo->getCodecCapsFor(mMediaType);
        mAAudioCaps = std::make_shared<const ACodecAudioCapabilities>(
                mCodecCaps->getAudioCapabilities());
        mAVideoCaps = std::make_shared<const ACodecVideoCapabilities>(
                mCodecCaps->getVideoCapabilities());
        mAEncoderCaps = std::make_shared<const ACodecEncoderCapabilities>(
                mCodecCaps->getEncoderCapabilities());
    }
};

#endif //_NDK_MEDIA_CODEC_INFO_PRIV_H