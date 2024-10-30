#include "libcxx_mpeg4extractor.hpp"

std::unique_ptr<MPEG4Extractor> create_mpeg4_extractor(rust::Str path, int32_t& err) {
    auto extractor =
        std::make_unique<MPEG4Extractor>(static_cast<std::string>(path).c_str());

    err = extractor->initCheck();

    if (err) {
        return nullptr;
    }

    return extractor;
}

void mpeg4_read_structure(MPEG4Extractor& extractor) {
    off_t offset = 0;
    while (extractor.read_chunk(&offset, 0 /* depth */) == 0) {
    }
}

size_t mpeg4_count_tracks(const MPEG4Extractor& extractor) {
    return extractor.countTracks();
}

std::shared_ptr<MPEG4Track> mpeg4_get_track(
        MPEG4Extractor& extractor, size_t index) {

    return extractor.trackAt(index);
}

int32_t mpeg4_read_at(
        MPEG4Extractor& extractor,
        uint64_t offset,
        rust::Slice<uint8_t> slice) {

    return extractor.readAt(offset, slice.data(), slice.size());
}

rust::String mpeg4_track_get_type(const MPEG4Track& track) {
    return rust::String::lossy(track.type());
}

uint32_t mpeg4_track_get_width(const MPEG4Track& track) {
    return track.width();
}

uint32_t mpeg4_track_get_height(const MPEG4Track& track) {
    return track.height();
}

bool mpeg4_track_get_codec_specific_data(
        const MPEG4Track& track, uint32_t fourcc, rust::Vec<uint8_t>& result) {

    auto optCSD = track.getCodecSpecificData(fourcc);
    if (!optCSD) {
        return false;
    }

    result.clear();
    std::copy(optCSD->begin(), optCSD->end(), std::back_inserter(result));

    return true;
}

bool mpeg4_track_get_sample_at(
        const MPEG4Track& track, size_t index, uint64_t& _offset, size_t& size) {

    off_t offset;
    auto success = track.getSampleAt(index, &offset, &size);

    _offset = offset;

    return success;
}
