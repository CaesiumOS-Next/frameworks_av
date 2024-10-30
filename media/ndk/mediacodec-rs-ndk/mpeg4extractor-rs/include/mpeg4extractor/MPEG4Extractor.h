// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <vector>

struct MPEG4Extractor {
    struct Track;

    explicit MPEG4Extractor(const char* path);
    ~MPEG4Extractor();

    int initCheck() const;
    int read_chunk(off_t *offset, int32_t depth);

    size_t countTracks() const;
    std::shared_ptr<Track> trackAt(size_t i);

    int readAt(off_t offset, void* data, size_t size);

private:
    enum class FlagBits {
        kSawMOOV        = 1,
    };

    int mInitCheck;
    int mFd;
    uint32_t mFlags;
    std::vector<std::shared_ptr<Track>> mTracks;

    int parseTrackHeader(size_t size);
    int parseMediaHeader(size_t size);

    int parseVideoFormat(
            uint32_t chunkType, off_t offset, size_t size, int32_t depth);

    int parseAudioFormat(
            uint32_t chunkType, off_t offset, size_t size, int32_t depth);

    int parseChunkOffsets(uint32_t chunk_type, size_t size);
    int parseSampleSizes(uint32_t chunk_type, size_t size);
    int parseSample2Chunk(size_t size);

    MPEG4Extractor(const MPEG4Extractor&) = delete;
    MPEG4Extractor& operator=(const MPEG4Extractor&) = delete;
};

struct MPEG4Extractor::Track {
    explicit Track();

    void setId(int32_t id);
    int32_t id() const;

    void setDimensions(uint32_t width, uint32_t height);
    uint32_t width() const;
    uint32_t height() const;

    void setType(std::string_view type);
    std::string type() const;

    void setTimeScale(uint32_t timeScale);
    uint32_t timeScale() const;

    void setDuration(uint64_t duration);
    uint64_t duration() const;

    void addCodecSpecificData(uint32_t type, std::vector<uint8_t>&& data);
    std::optional<std::vector<uint8_t>> getCodecSpecificData(uint32_t type) const;

    void addChunkOffset(off_t offset);
    bool chunkOffsetsValid() const;

    void setDefaultSampleSize(size_t numSamples, size_t defaultSampleSize);

    void setSampleSizes(
            std::vector<uint8_t>&& data,
            size_t numSamples,
            size_t sampleSizeBitsLog2);

    bool sampleSizesValid() const;

    void setSample2Chunk(std::vector<uint8_t>&& data, size_t numEntries);
    bool sample2ChunkValid() const;

    std::optional<size_t> getSampleSizeAt(size_t i) const;
    bool getSampleAt(size_t i, off_t* sampleOffset, size_t* sampleSize) const;

    size_t countChunks() const;

private:
    enum class FlagBits {
        kHasId                  = 1,
        kHasDimensions          = 2,
        kHasType                = 4,
        kHasTimeScale           = 8,
        kHasDuration            = 16,
        kHasChunkOffsets        = 32,
        kHasSampleSizes         = 64,
        kHasSample2Chunk        = 128,
    };

    struct SampleSizeIterator {
        virtual ~SampleSizeIterator() = default;

        virtual std::optional<size_t> at(size_t i) const = 0;
    };

    struct SampleSizeData : public SampleSizeIterator {
        SampleSizeData(
                std::vector<uint8_t>&& data,
                size_t numSamples,
                size_t sampleSizeBitsLog2);

        std::optional<size_t> at(size_t i) const override;

    private:
        std::vector<uint8_t> mData;
        size_t mNumSamples;
        size_t mSampleSizeBitsLog2;
    };

    struct DefaultSampleSizeData : public SampleSizeIterator {
        explicit DefaultSampleSizeData(size_t numSamples, size_t sampleSize);

        std::optional<size_t> at(size_t i) const override;

    private:
        size_t mNumSamples;
        size_t mSampleSize;

        DefaultSampleSizeData(const DefaultSampleSizeData&) = delete;
        DefaultSampleSizeData& operator=(const DefaultSampleSizeData&) = delete;
    };

    struct Sample2ChunkData {
        explicit Sample2ChunkData(
                std::vector<uint8_t>&& data, size_t numEntries);

        std::optional<std::tuple<size_t, size_t, size_t>> at(size_t i) const;

    private:
        std::vector<uint8_t> mData;
        size_t mNumEntries;
    };

    uint32_t mFlags;

    std::unordered_map<uint32_t, std::vector<uint8_t>> mCodecSpecificData;
    std::vector<off_t> mChunkOffsets;
    std::unique_ptr<Sample2ChunkData> mSample2ChunkData;

    std::string mType;
    uint64_t mDuration;
    int32_t mId;
    uint32_t mWidth, mHeight;
    uint32_t mTimeScale;
    std::unique_ptr<SampleSizeIterator> mSampleSizes;
};
