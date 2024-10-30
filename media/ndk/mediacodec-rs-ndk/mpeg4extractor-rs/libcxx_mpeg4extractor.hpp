#pragma once

#include "mpeg4extractor/MPEG4Extractor.h"

#include <rust/cxx.h>

std::unique_ptr<MPEG4Extractor> create_mpeg4_extractor(rust::Str path, int32_t& err);
void mpeg4_read_structure(MPEG4Extractor& extractor);

size_t mpeg4_count_tracks(const MPEG4Extractor& extractor);

typedef MPEG4Extractor::Track MPEG4Track;
std::shared_ptr<MPEG4Track> mpeg4_get_track(MPEG4Extractor& extractor, size_t index);

int32_t mpeg4_read_at(
        MPEG4Extractor& extractor, uint64_t offset, rust::Slice<uint8_t> slice);

rust::String mpeg4_track_get_type(const MPEG4Track& track);

uint32_t mpeg4_track_get_width(const MPEG4Track& track);
uint32_t mpeg4_track_get_height(const MPEG4Track& track);

bool mpeg4_track_get_codec_specific_data(
        const MPEG4Track& track, uint32_t fourcc, rust::Vec<uint8_t>& result);

bool mpeg4_track_get_sample_at(
        const MPEG4Track& track, size_t index, uint64_t& offset, size_t& size);
