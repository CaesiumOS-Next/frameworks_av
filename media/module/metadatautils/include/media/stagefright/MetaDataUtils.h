/*
 * Copyright 2018 The Android Open Source Project
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

#ifndef META_DATA_UTILS_H_

#define META_DATA_UTILS_H_

#include <media/stagefright/MetaData.h>

struct AMediaFormat;

namespace android {

enum {
    apvPrimaryFrame = 1,
    apvNonPrimaryFrame = 2,
    apvPreviewFrame = 25,
    apvDepthFrame = 26,
    apvAlphaFrame = 27,
};

struct ABuffer;
struct ApvFrameHeader {
    uint8_t profile_idc;
    uint8_t level_idc;
    uint8_t band_idc;
    uint32_t frame_width_minus1;
    uint32_t frame_height_minus1;
    uint8_t chroma_format_idc;
    uint8_t bit_depth_minus8;
    uint8_t capture_time_distance;
    bool color_description_present_flag;
    uint8_t color_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;
    bool use_q_matrix;
    uint8_t size_of_q_matrix_info;
    uint8_t* quantization_matrix;
    uint16_t size_of_tile_info;  // bytes
    uint8_t* tile_info;
};

bool MakeAVCCodecSpecificData(MetaDataBase &meta, const uint8_t *data, size_t size);
bool MakeAVCCodecSpecificData(AMediaFormat *meta, const uint8_t *data, size_t size);

bool MakeAACCodecSpecificData(MetaDataBase &meta, const uint8_t *data, size_t size);
bool MakeAACCodecSpecificData(MetaDataBase &meta, unsigned profile, unsigned sampling_freq_index,
        unsigned channel_configuration);

bool MakeAACCodecSpecificData(AMediaFormat *meta, unsigned profile, unsigned sampling_freq_index,
        unsigned channel_configuration);

void parseVorbisComment(
        AMediaFormat *fileMeta, const char *comment, size_t commentLength);

ApvFrameHeader parseAPVDecoderConfiguration(const void* data, size_t size);
void updateFrameHeader(AMediaFormat* fileMeta, const ApvFrameHeader& header);

bool MakeVP9CodecSpecificData(AMediaFormat* meta, int32_t csdSize, int32_t profile, int32_t level,
                              int32_t bitDepth, int32_t chromaSubsampling);
bool MakeVP9CodecSpecificDataFromFirstFrame(AMediaFormat* meta, const uint8_t* data, size_t size);
bool MakeVP9CodecPrivateFromVpcC(AMediaFormat* meta, const uint8_t* data, size_t size);

}  // namespace android

#endif  // META_DATA_UTILS_H_
