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

//#define LOG_NDEBUG 0
#define LOG_TAG "MPEG4Extractor"
#include <utils/Log.h>

#include "mpeg4extractor/MPEG4Extractor.h"

#include "mpeg4extractor/ESDS.h"
#include "mpeg4extractor/Utils.h"

#include <android-base/logging.h>
#include <byteswap.h>
#include <cerrno>
#include <cinttypes>
#include <fcntl.h>

static_assert(sizeof(off_t) == 8);

static std::string MakeFourCC(uint32_t chunk_type) {
    char c1 = chunk_type >> 24;
    char c2 = (chunk_type >> 16) & 0xff;
    char c3 = (chunk_type >> 8) & 0xff;
    char c4 = chunk_type & 0xff;

    if (isprint(c1) && isprint(c2) && isprint(c3) && isprint(c4)) {
        return StringPrintf("'%c%c%c%c'", c1, c2, c3, c4);
    }

    return StringPrintf("0x%08x", chunk_type);
}

static uint16_t U16_AT(const void* _data) {
    auto data = static_cast<const uint8_t*>(_data);
    return (uint16_t(data[0]) << 8) | data[1];
}

static uint32_t U32_AT(const void* _data) {
    auto data = static_cast<const uint8_t*>(_data);
    uint32_t out;
    memcpy(&out, data, sizeof(out));

    return ntohl(out);
}

static uint64_t U64_AT(const void* _data) {
    auto data = static_cast<const uint8_t*>(_data);
    uint64_t out;
    memcpy(&out, data, sizeof(out));

    return be64toh(out);
}

MPEG4Extractor::Track::Track()
    : mFlags(0) {
}

void MPEG4Extractor::Track::setId(int32_t id) {
    CHECK(!(mFlags & static_cast<uint32_t>(FlagBits::kHasId)));
    mId = id;
    mFlags |= static_cast<uint32_t>(FlagBits::kHasId);
}

int32_t MPEG4Extractor::Track::id() const {
    CHECK(mFlags & static_cast<uint32_t>(FlagBits::kHasId));
    return mId;
}

void MPEG4Extractor::Track::setDimensions(uint32_t width, uint32_t height) {
    CHECK(!(mFlags & static_cast<uint32_t>(FlagBits::kHasDimensions)));
    mWidth = width;
    mHeight = height;
    mFlags |= static_cast<uint32_t>(FlagBits::kHasDimensions);
}

uint32_t MPEG4Extractor::Track::width() const {
    CHECK(mFlags & static_cast<uint32_t>(FlagBits::kHasDimensions));
    return mWidth;
}

uint32_t MPEG4Extractor::Track::height() const {
    CHECK(mFlags & static_cast<uint32_t>(FlagBits::kHasDimensions));
    return mHeight;
}

void MPEG4Extractor::Track::setType(std::string_view type) {
    static constexpr auto mask = static_cast<uint32_t>(FlagBits::kHasType);

    if (mFlags & mask) {
        CHECK(mType == "application/octet-stream");
    }

    mType = type;
    mFlags |= mask;
}

std::string MPEG4Extractor::Track::type() const {
    CHECK(mFlags & static_cast<uint32_t>(FlagBits::kHasType));
    return mType;
}

void MPEG4Extractor::Track::setTimeScale(uint32_t timeScale) {
    CHECK(!(mFlags & static_cast<uint32_t>(FlagBits::kHasTimeScale)));
    mTimeScale = timeScale;
    mFlags |= static_cast<uint32_t>(FlagBits::kHasTimeScale);
}

uint32_t MPEG4Extractor::Track::timeScale() const {
    CHECK(mFlags & static_cast<uint32_t>(FlagBits::kHasTimeScale));
    return mTimeScale;
}

void MPEG4Extractor::Track::setDuration(uint64_t duration) {
    CHECK(!(mFlags & static_cast<uint32_t>(FlagBits::kHasDuration)));
    mDuration = duration;
    mFlags |= static_cast<uint32_t>(FlagBits::kHasDuration);
}

uint64_t MPEG4Extractor::Track::duration() const {
    CHECK(mFlags & static_cast<uint32_t>(FlagBits::kHasDuration));
    return mDuration;
}

void MPEG4Extractor::Track::addCodecSpecificData(
        uint32_t type, std::vector<uint8_t>&& data) {

    auto success = mCodecSpecificData.insert(std::make_pair(type, data)).second;
    CHECK(success);
}

std::optional<std::vector<uint8_t>> MPEG4Extractor::Track::getCodecSpecificData(
        uint32_t type) const {

    auto it = mCodecSpecificData.find(type);

    if (it == mCodecSpecificData.end()) {
        return std::nullopt;
    }

    if (type == FOURCC("esds")) {
        if (it->second.size() < 4) {
            return std::nullopt;
        }

        android::ESDS esds(it->second.data() + 4, it->second.size() - 4);
        if (esds.InitCheck() != 0) {
            return std::nullopt;
        }

        const void* csd_data;
        size_t csd_size;
        if (esds.getCodecSpecificInfo(&csd_data, &csd_size) != 0) {
            return std::nullopt;
        }

        std::vector<uint8_t> copy(csd_size);
        memcpy(copy.data(), csd_data, csd_size);

        return copy;
    }

    return it->second;
}

void MPEG4Extractor::Track::addChunkOffset(off_t offset) {
    mChunkOffsets.push_back(offset);

    mFlags |= static_cast<uint32_t>(FlagBits::kHasChunkOffsets);
}

bool MPEG4Extractor::Track::chunkOffsetsValid() const {
    return (mFlags & static_cast<uint32_t>(FlagBits::kHasChunkOffsets)) != 0;
}

void MPEG4Extractor::Track::setDefaultSampleSize(
        size_t numSamples, size_t defaultSampleSize) {

    mSampleSizes =
        std::make_unique<DefaultSampleSizeData>(numSamples, defaultSampleSize);

    mFlags |= static_cast<uint32_t>(FlagBits::kHasSampleSizes);
}

void MPEG4Extractor::Track::setSampleSizes(
        std::vector<uint8_t>&& data,
        size_t numSamples,
        size_t sampleSizeBitsLog2) {

    mSampleSizes = std::make_unique<SampleSizeData>(
            std::move(data), numSamples, sampleSizeBitsLog2);

    mFlags |= static_cast<uint32_t>(FlagBits::kHasSampleSizes);
}

bool MPEG4Extractor::Track::sampleSizesValid() const {
    return (mFlags & static_cast<uint32_t>(FlagBits::kHasSampleSizes)) != 0;
}

void MPEG4Extractor::Track::setSample2Chunk(
        std::vector<uint8_t>&& data, size_t numEntries) {

    mSample2ChunkData =
        std::make_unique<Sample2ChunkData>(std::move(data), numEntries);

    mFlags |= static_cast<uint32_t>(FlagBits::kHasSample2Chunk);
}

bool MPEG4Extractor::Track::sample2ChunkValid() const {
    return (mFlags & static_cast<uint32_t>(FlagBits::kHasSample2Chunk)) != 0;
}

bool MPEG4Extractor::Track::getSampleAt(
        size_t i, off_t* sampleOffset, size_t* sampleSize) const {

    CHECK(mFlags & (static_cast<uint32_t>(FlagBits::kHasSample2Chunk)
                | static_cast<uint32_t>(FlagBits::kHasChunkOffsets)
                | static_cast<uint32_t>(FlagBits::kHasSampleSizes)));

    auto entry = mSample2ChunkData->at(0);

    if (!entry.has_value()) {
        return false;
    }

    size_t lastSampleIndex = 0;
    auto [lastChunkIndex, lastNumSamples, _sampleDescIndex] = *entry;

    size_t entryIndex = 1;
    for (;;) {
        auto entry = mSample2ChunkData->at(entryIndex);

        size_t chunkIndex;
        if (!entry.has_value()) {
            chunkIndex = countChunks() + 1;
        } else {
            chunkIndex = std::get<0>(*entry);
        }

        auto numChunks = chunkIndex - lastChunkIndex;

        if (i >= lastSampleIndex + numChunks * lastNumSamples) {
            if (!entry.has_value()) {
                return false;
            }

            lastSampleIndex += numChunks * lastNumSamples;
            lastChunkIndex = chunkIndex;
            lastNumSamples = std::get<1>(*entry);

            ++entryIndex;
            continue;
        }

        size_t skippedChunks = (i - lastSampleIndex) / lastNumSamples;
        size_t sampleChunkIndex = lastChunkIndex - 1 + skippedChunks;

        CHECK(sampleChunkIndex <= mChunkOffsets.size());
        auto chunkOffset = mChunkOffsets[sampleChunkIndex];

        *sampleOffset = chunkOffset;

        for (size_t j = lastSampleIndex + skippedChunks * lastNumSamples; j < i; ++j) {
            auto optSampleSize = mSampleSizes->at(j);
            CHECK(optSampleSize.has_value());
            *sampleOffset += *optSampleSize;
        }

        auto optSampleSize = mSampleSizes->at(i);
        CHECK(optSampleSize.has_value());

        *sampleSize = *optSampleSize;
        break;
    }

    return true;
}

std::optional<size_t> MPEG4Extractor::Track::getSampleSizeAt(size_t i) const {
    CHECK(mFlags & static_cast<uint32_t>(FlagBits::kHasSampleSizes));

    return mSampleSizes->at(i);
}

size_t MPEG4Extractor::Track::countChunks() const {
    CHECK(mFlags & static_cast<uint32_t>(FlagBits::kHasChunkOffsets));

    return mChunkOffsets.size();
}

////////////////////////////////////////////////////////////////////////////////

MPEG4Extractor::Track::DefaultSampleSizeData::DefaultSampleSizeData(
        size_t numSamples, size_t sampleSize)
    : mNumSamples(numSamples),
      mSampleSize(sampleSize) {
}

std::optional<size_t> MPEG4Extractor::Track::DefaultSampleSizeData::at(size_t i) const {
    if (i >= mNumSamples) {
        return std::nullopt;
    }

    return mSampleSize;
}

////////////////////////////////////////////////////////////////////////////////

MPEG4Extractor::Track::SampleSizeData::SampleSizeData(
        std::vector<uint8_t>&& data,
        size_t numSamples,
        size_t sampleSizeBitsLog2)
    : mData(data),
      mNumSamples(numSamples),
      mSampleSizeBitsLog2(sampleSizeBitsLog2) {
}

std::optional<size_t> MPEG4Extractor::Track::SampleSizeData::at(size_t i) const {
    if (i >= mNumSamples) {
        return std::nullopt;
    }

    switch (mSampleSizeBitsLog2) {
        case 2:
        {
            auto byte = mData[i / 2];
            return (i & 1) ? byte >> 4 : byte & 0xf;
        }

        case 3:
            return mData[i];

        case 4:
            return U16_AT(&mData[2 * i]);

        case 5:
            return U32_AT(&mData[4 * i]);

        default:
            CHECK(!"Should not be here.");
            return std::nullopt;
    }

}

////////////////////////////////////////////////////////////////////////////////

MPEG4Extractor::Track::Sample2ChunkData::Sample2ChunkData(
        std::vector<uint8_t>&& data, size_t numEntries)
    : mData(data),
      mNumEntries(numEntries) {
}

std::optional<std::tuple<size_t, size_t, size_t>>
MPEG4Extractor::Track::Sample2ChunkData::at(size_t i) const {

    if (i >= mNumEntries) {
        return std::nullopt;
    }

    const auto* src = &mData[i * 12];
    return std::tuple(U32_AT(src), U32_AT(src + 4), U32_AT(src + 8));
}

////////////////////////////////////////////////////////////////////////////////

MPEG4Extractor::MPEG4Extractor(const char* path)
    : mInitCheck(-ENODEV),
      mFlags(0) {

    mFd = open(path, O_RDONLY);
    if (mFd < 0) {
        mInitCheck = -errno;
        goto bail;
    }

    mInitCheck = 0;
    return;

bail:
    ;
}

MPEG4Extractor::~MPEG4Extractor() {
    if (mInitCheck) {
        return;
    }

    close(mFd);
    mFd = -1;
}

int MPEG4Extractor::initCheck() const {
    return mInitCheck;
}

int MPEG4Extractor::read_chunk(off_t* offset, int32_t depth) {
    if (mInitCheck) {
        return mInitCheck;
    }

    if (lseek(mFd, *offset, SEEK_SET) == -1) {
        return -errno;
    }

    auto chunk_offset = *offset;

    uint32_t hdr[2];
    if (read(mFd, hdr, sizeof(hdr)) != sizeof(hdr)) {
        return -EIO;
    }

    *offset += sizeof(hdr);

    uint64_t chunk_size = ntohl(hdr[0]);
    int32_t chunk_type = ntohl(hdr[1]);

    if (chunk_size == 1) {
        if (read(mFd, &chunk_size, sizeof(chunk_size)) != sizeof(chunk_size)) {
            return -EIO;
        }

        *offset += sizeof(chunk_size);

        chunk_size = be64toh(chunk_size);

        if (chunk_size < 16) {
            // The smallest valid chunk is 16 bytes long in this case.
            return -EINVAL;
        }
    } else if (chunk_size == 0) {
        if (depth) {
            // Invalid for non-toplevel atoms, skip it.
            return 0;
        }

        auto file_size = lseek(mFd, 0, SEEK_END);

        if (file_size == -1) {
            return -errno;
        }

        if (lseek(mFd, *offset, SEEK_SET) == -1) {
            return -errno;
        }

        chunk_size = file_size - *offset;

    }

    if (chunk_size < 8) {
        // The smallest valid chunk is 8 bytes long.
        return -EINVAL;
    }

    auto s = MakeFourCC(chunk_type);

    ALOGV("Found chunk %s @ 0x%016" PRIx64 " of size %" PRIu64" at depth %d",
          s.c_str(),
          chunk_offset,
          chunk_size,
          depth);

    auto chunk_data_size = chunk_size - (*offset - chunk_offset);

    switch (chunk_type) {
        case FOURCC("moov"):
        case FOURCC("trak"):
        case FOURCC("mdia"):
        case FOURCC("minf"):
        case FOURCC("dinf"):
        case FOURCC("stbl"):
        case FOURCC("mvex"):
        case FOURCC("moof"):
        case FOURCC("traf"):
        case FOURCC("mfra"):
        case FOURCC("udta"):
        case FOURCC("ilst"):
        case FOURCC("sinf"):
        case FOURCC("schi"):
        case FOURCC("edts"):
        case FOURCC("wave"):
        {
            if (chunk_type == FOURCC("moov")) {
                if (depth != 0
                        || (mFlags & static_cast<uint32_t>(
                                FlagBits::kSawMOOV))) {
                    return -EINVAL;
                }

                mFlags |= static_cast<uint32_t>(FlagBits::kSawMOOV);
            }

            if (chunk_type == FOURCC("trak")) {
                if (depth != 1) {
                    return -EINVAL;
                }

                auto track = std::make_shared<Track>();
                track->setType("application/octet-stream");

                mTracks.push_back(std::move(track));
            }

            off_t stop_offset = *offset + chunk_data_size;
            while (*offset < stop_offset) {
                auto err = read_chunk(offset, depth + 1);

                if (err) {
                    return err;
                }
            }

            if (*offset != stop_offset) {
                return -EINVAL;
            }

            break;
        }

        case FOURCC("tkhd"):
        {
            auto err = parseTrackHeader(chunk_data_size);

            if (err) {
                return err;
            }

            *offset += chunk_data_size;
            if (lseek(mFd, *offset, SEEK_SET) == -1) {
                return -errno;
            }
            break;
        }

        case FOURCC("mdhd"):
        {
            auto err = parseMediaHeader(chunk_data_size);

            if (err) {
                return err;
            }

            *offset += chunk_data_size;
            if (lseek(mFd, *offset, SEEK_SET) == -1) {
                return -errno;
            }
            break;
        }

        case FOURCC("stsd"):
        {
            uint8_t buffer[8];
            if (chunk_data_size < sizeof(buffer)) {
                return -EINVAL;
            }

            if (read(mFd, buffer, sizeof(buffer)) != sizeof(buffer)) {
                return -EINVAL;
            }

            if (U32_AT(buffer) != 0) {
                // Should be version 0, flags 0.
                return -EINVAL;
            }

            auto num_entries = U32_AT(&buffer[4]);

            auto stop_offset = *offset + chunk_data_size;
            *offset += sizeof(buffer);

            for (uint32_t i = 0; i < num_entries; ++i) {
                auto err = read_chunk(offset, depth + 1);

                if (err) {
                    return err;
                }
            }

            if (*offset != stop_offset) {
                return -EINVAL;
            }
            break;
        }

        case FOURCC("mp4a"):
        {
            auto err = parseAudioFormat(
                    chunk_type, *offset, chunk_data_size, depth);

            if (err) {
                return err;
            }

            *offset += chunk_data_size;
            if (lseek(mFd, *offset, SEEK_SET) == -1) {
                return -errno;
            }
            break;
        }

        case FOURCC("avc1"):
        case FOURCC("mp4v"):
        case FOURCC("hev1"):
        case FOURCC("av01"):
        case FOURCC("vp09"):
        {
            auto err = parseVideoFormat(
                    chunk_type, *offset, chunk_data_size, depth);

            if (err) {
                return err;
            }

            *offset += chunk_data_size;
            if (lseek(mFd, *offset, SEEK_SET) == -1) {
                return -errno;
            }
            break;
        }

        case FOURCC("avcC"):
        case FOURCC("pasp"):
        case FOURCC("hvcC"):
        case FOURCC("av1C"):
        case FOURCC("vpcC"):
        case FOURCC("esds"):
        {
            if (mTracks.size() == 0) {
                return -EINVAL;
            }

            auto* track = mTracks.back().get();

            std::vector<uint8_t> buffer(chunk_data_size);
            if (read(mFd, buffer.data(), buffer.size()) != buffer.size()) {
                return -EINVAL;
            }

            track->addCodecSpecificData(chunk_type, std::move(buffer));

            *offset += chunk_data_size;
            break;
        }

        case FOURCC("stco"):
        case FOURCC("co64"):
        {
            auto err = parseChunkOffsets(chunk_type, chunk_data_size);

            if (err) {
                return err;
            }

            *offset += chunk_data_size;
            if (lseek(mFd, *offset, SEEK_SET) == -1) {
                return -errno;
            }
            break;
        }

        case FOURCC("stsz"):
        case FOURCC("stz2"):
        {
            auto err = parseSampleSizes(chunk_type, chunk_data_size);

            if (err) {
                return err;
            }

            *offset += chunk_data_size;
            if (lseek(mFd, *offset, SEEK_SET) == -1) {
                return -errno;
            }
            break;
        }

        case FOURCC("stsc"):
        {
            auto err = parseSample2Chunk(chunk_data_size);

            if (err) {
                return err;
            }

            *offset += chunk_data_size;
            if (lseek(mFd, *offset, SEEK_SET) == -1) {
                return -errno;
            }
            break;
            break;
        }

        default:
            *offset += chunk_data_size;
            break;
    }

    return 0;
}

int MPEG4Extractor::parseTrackHeader(size_t size) {
    if (size < 1) {
        return -EINVAL;
    }

    uint8_t version;
    if (read(mFd, &version, 1) != 1) {
        return -EINVAL;
    }

    if (version != 0 && version != 1) {
        return -EINVAL;
    }

    auto dynSize = (version == 1) ? 36 : 24;

    if (size != dynSize + 60) {
        return -EINVAL;
    }

    uint8_t buffer[36 + 60];
    buffer[0] = version;

    if (read(mFd, &buffer[1], dynSize + 60 - 1) != dynSize + 60 - 1) {
        return -EINVAL;
    }

    if (mTracks.size() == 0) {
        return -EINVAL;
    }

    auto* track = mTracks.back().get();

    int32_t id = (version == 0) ? U32_AT(&buffer[12]) : U32_AT(&buffer[20]);
    auto width = U32_AT(&buffer[dynSize + 52]);
    auto height = U32_AT(&buffer[dynSize + 56]);

    track->setId(id);
    track->setDimensions(width >> 16, height >> 16);

    return 0;
}

int MPEG4Extractor::parseMediaHeader(size_t size) {

    uint8_t buffer[24];

    if (size != sizeof(buffer)) {
        return -EINVAL;
    }

    if (read(mFd, buffer, sizeof(buffer)) != sizeof(buffer)) {
        return -EINVAL;
    }

    if (mTracks.size() == 0) {
        return -EINVAL;
    }

    auto* track = mTracks.back().get();

    auto version = buffer[0];

    auto timescale_offset = (version == 0) ? 4 + 8 : 4 + 16;

    auto timescale = U32_AT(&buffer[timescale_offset]);

    uint64_t duration;
    if (version == 0) {
        duration = U32_AT(&buffer[timescale_offset + 4]);
    } else {
        duration = U64_AT(&buffer[timescale_offset + 4]);
    }

    track->setTimeScale(timescale);
    track->setDuration(duration);

    ALOGV("timeScale: 0x%" PRIx32, timescale);
    ALOGV("duration: 0x%" PRIx64, duration);

    if (duration && timescale) {
        long double durationUs = ((long double)duration * 1e6) / timescale;
        (void)durationUs;
        ALOGV("duration: %Lf us", durationUs);
    }

    return 0;
}

int MPEG4Extractor::parseVideoFormat(
        uint32_t chunkType, off_t offset, size_t size, int32_t depth) {

    uint8_t buffer[78];
    if (size < sizeof(buffer)) {
        // Basic VideoSampleEntry size.
        return -EINVAL;
    }

    if (read(mFd, buffer, sizeof(buffer)) != sizeof(buffer)) {
        return -EINVAL;
    }

    if (mTracks.size() == 0) {
        return -EINVAL;
    }

    auto* track = mTracks.back().get();

    switch (chunkType) {
        case FOURCC("avc1"):
            track->setType("video/avc");
            break;

        case FOURCC("mp4v"):
            track->setType("video/mp4v-es");
            break;

        case FOURCC("hev1"):
            track->setType("video/hevc");
            break;

        case FOURCC("av01"):
            track->setType("video/av01");
            break;

        case FOURCC("vp09"):
            track->setType("video/x-vnd.on2.vp9");
            break;

        default:
            break;
    }

    // auto width = U16_AT(&buffer[6 + 18]);
    // auto height = U16_AT(&buffer[6 + 20]);

    auto stop_offset = offset + size;
    offset += sizeof(buffer);
    while (offset < stop_offset) {
        auto err = read_chunk(&offset, depth + 1);
        if (err) {
            return err;
        }
    }

    if (offset != stop_offset) {
        return -EINVAL;
    }

    return 0;
}

int MPEG4Extractor::parseAudioFormat(
        uint32_t chunkType, off_t offset, size_t size, int32_t depth) {

    uint8_t buffer[8 + 20];
    if (size < sizeof(buffer)) {
        // Basic AudioSampleEntry size.
        return -EINVAL;
    }

    if (read(mFd, buffer, sizeof(buffer)) != sizeof(buffer)) {
        return -EINVAL;
    }

    if (mTracks.size() == 0) {
        return -EINVAL;
    }

    auto* track = mTracks.back().get();

    switch (chunkType) {
        case FOURCC("mp4a"):
            track->setType("audio/aac");
            break;

        default:
            break;
    }

    // auto numChannels = U16_AT(&buffer[16]);
    // auto sampleSize = U16_AT(&buffer[18]);
    // auto sampleRate = U32_AT(&buffer[24]) >> 16;

    auto stop_offset = offset + size;
    offset += sizeof(buffer);
    while (offset < stop_offset) {
        auto err = read_chunk(&offset, depth + 1);
        if (err) {
            return err;
        }
    }

    if (offset != stop_offset) {
        return -EINVAL;
    }

    return 0;
}
int MPEG4Extractor::parseChunkOffsets(uint32_t chunkType, size_t size) {

    if (mTracks.size() == 0) {
        return -EINVAL;
    }

    auto* track = mTracks.back().get();

    if (track->chunkOffsetsValid()) {
        return -EINVAL;
    }

    uint8_t header[8];

    if (size < sizeof(header)) {
        return -EINVAL;
    }

    if (read(mFd, header, sizeof(header)) != sizeof(header)) {
        return -EINVAL;
    }

    if (U32_AT(header) != 0) {
        // Expected version = 0, flags = 0.
        return -EINVAL;
    }

    auto num_chunk_offsets = U32_AT(&header[4]);
    auto bytes_per_chunk_offset_shift = (chunkType == FOURCC("stco")) ? 2 : 3;

    if (size < 8 + (num_chunk_offsets << bytes_per_chunk_offset_shift)) {
        return -EINVAL;
    }

    std::vector<uint8_t> buffer(
            num_chunk_offsets << bytes_per_chunk_offset_shift);

    if (read(mFd, buffer.data(), buffer.size()) != buffer.size()) {
        return -EINVAL;
    }

    if (bytes_per_chunk_offset_shift == 2) {
        for (uint32_t i = 0; i < num_chunk_offsets; ++i) {
            auto chunk_offset = U32_AT(buffer.data() + 4 * i);
            track->addChunkOffset(chunk_offset);
        }
    } else {
        for (uint32_t i = 0; i < num_chunk_offsets; ++i) {
            auto chunk_offset = U64_AT(buffer.data() + 8 * i);
            track->addChunkOffset(chunk_offset);
        }
    }

    return 0;
}

int MPEG4Extractor::parseSampleSizes(uint32_t chunkType, size_t size) {

    if (mTracks.size() == 0) {
        return -EINVAL;
    }

    auto* track = mTracks.back().get();

    if (track->sampleSizesValid()) {
        return -EINVAL;
    }

    uint8_t header[12];

    if (size < sizeof(header)) {
        return -EINVAL;
    }

    if (read(mFd, header, sizeof(header)) != sizeof(header)) {
        return -EINVAL;
    }

    if (U32_AT(header) != 0) {
        // Expected version = 0, flags = 0.
        return -EINVAL;
    }

    size_t defaultSampleSize = U32_AT(&header[4]);
    size_t numSampleSizes = U32_AT(&header[8]);

    size_t sampleSizeBitsLog2;
    if (chunkType == FOURCC("stsz")) {
        if (defaultSampleSize) {
            track->setDefaultSampleSize(numSampleSizes, defaultSampleSize);
            return 0;
        }

        sampleSizeBitsLog2 = 5;

    } else {
        if (defaultSampleSize & ~0xff) {
            // The high 24 bits are reserved and must be 0.
            return -EINVAL;
        }

        switch (defaultSampleSize) {
            case 4:
                sampleSizeBitsLog2 = 2;
                break;
            case 8:
                sampleSizeBitsLog2 = 3;
                break;
            case 16:
                sampleSizeBitsLog2 = 4;
                break;
            default:
                return -EINVAL;
        }
    }

    size_t payloadSize = ((numSampleSizes << sampleSizeBitsLog2) + 7) / 8;

    if (size < 12 + payloadSize) {
        return -EINVAL;
    }

    std::vector<uint8_t> buffer(payloadSize);

    if (read(mFd, buffer.data(), buffer.size()) != buffer.size()) {
        return -EINVAL;
    }

    track->setSampleSizes(
            std::move(buffer), numSampleSizes, sampleSizeBitsLog2);

    return 0;
}

int MPEG4Extractor::parseSample2Chunk(size_t size) {

    if (mTracks.size() == 0) {
        return -EINVAL;
    }

    auto* track = mTracks.back().get();

    if (track->sample2ChunkValid()) {
        return -EINVAL;
    }

    uint8_t header[8];
    if (size < sizeof(header)) {
        return -EINVAL;
    }

    if (read(mFd, header, sizeof(header)) != sizeof(header)) {
        return -EINVAL;
    }

    if (U32_AT(header) != 0) {
        // Expected version = 0, flags = 0.
        return -EINVAL;
    }

    auto numEntries = U32_AT(&header[4]);

    if (size < 8 + numEntries * 12) {
        return -EINVAL;
    }

    std::vector<uint8_t> buffer(numEntries * 12);
    if (read(mFd, buffer.data(), buffer.size()) != buffer.size()) {
        return -EINVAL;
    }

    track->setSample2Chunk(std::move(buffer), numEntries);

    return 0;
}

size_t MPEG4Extractor::countTracks() const {
    return mTracks.size();
}

std::shared_ptr<MPEG4Extractor::Track> MPEG4Extractor::trackAt(size_t i) {
    CHECK(i < mTracks.size());
    return mTracks[i];
}

int MPEG4Extractor::readAt(off_t offset, void* data, size_t size) {
    auto prevOffset = lseek(mFd, offset, SEEK_SET);

    if (prevOffset == -1) {
        return -errno;
    }

    if (read(mFd, data, size) != size) {
        return -EIO;
    }

    auto res = lseek(mFd, prevOffset, SEEK_SET);

    if (res == -1) {
        return -errno;
    }

    return 0;
}
