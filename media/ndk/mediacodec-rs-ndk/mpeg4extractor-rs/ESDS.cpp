/*
 * Copyright (C) 2009 The Android Open Source Project
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
#define LOG_TAG "ESDS"
#include <utils/Log.h>

#include "mpeg4extractor/ESDS.h"

#include <byteswap.h>
#include <cerrno>
#include <cstring>

namespace android {

static uint32_t U32_AT(const void* _data) {
    auto data = static_cast<const uint8_t*>(_data);
    uint32_t out;
    memcpy(&out, data, sizeof(out));

    return ntohl(out);
}

ESDS::ESDS(const void *data, size_t size)
    : mData(new uint8_t[size]),
      mSize(size),
      mInitCheck(-ENODEV),
      mDecoderSpecificOffset(0),
      mDecoderSpecificLength(0),
      mObjectTypeIndication(0) {
    memcpy(mData, data, size);

    mInitCheck = parse();
}

ESDS::~ESDS() {
    delete[] mData;
    mData = NULL;
}

int ESDS::InitCheck() const {
    return mInitCheck;
}

int ESDS::getObjectTypeIndication(uint8_t *objectTypeIndication) const {
    if (mInitCheck != 0) {
        return mInitCheck;
    }

    *objectTypeIndication = mObjectTypeIndication;

    return 0;
}

int ESDS::getCodecSpecificInfo(const void **data, size_t *size) const {
    if (mInitCheck != 0) {
        return mInitCheck;
    }

    *data = &mData[mDecoderSpecificOffset];
    *size = mDecoderSpecificLength;

    return 0;
}

int ESDS::skipDescriptorHeader(
        size_t offset, size_t size,
        uint8_t *tag, size_t *data_offset, size_t *data_size) const {
    if (size == 0) {
        return -EINVAL;
    }

    *tag = mData[offset++];
    --size;

    *data_size = 0;
    bool more;
    do {
        if (size == 0) {
            return -EINVAL;
        }

        uint8_t x = mData[offset++];
        --size;

        *data_size = (*data_size << 7) | (x & 0x7f);
        more = (x & 0x80) != 0;
    }
    while (more);

    ALOGV("tag=0x%02x data_size=%zu", *tag, *data_size);

    if (*data_size > size) {
        return -EINVAL;
    }

    *data_offset = offset;

    return 0;
}

int ESDS::parse() {
    uint8_t tag;
    size_t data_offset;
    size_t data_size;
    int err =
        skipDescriptorHeader(0, mSize, &tag, &data_offset, &data_size);

    if (err != 0) {
        return err;
    }

    if (tag != kTag_ESDescriptor) {
        return -EINVAL;
    }

    return parseESDescriptor(data_offset, data_size);
}

int ESDS::parseESDescriptor(size_t offset, size_t size) {
    if (size < 3) {
        return -EINVAL;
    }

    offset += 2;  // skip ES_ID
    size -= 2;

    unsigned streamDependenceFlag = mData[offset] & 0x80;
    unsigned URL_Flag = mData[offset] & 0x40;
    unsigned OCRstreamFlag = mData[offset] & 0x20;

    ++offset;
    --size;

    if (streamDependenceFlag) {
        if (size < 2)
            return -EINVAL;
        offset += 2;
        size -= 2;
    }

    if (URL_Flag) {
        if (offset >= size) {
            return -EINVAL;
        }
        unsigned URLlength = mData[offset];
        if (URLlength >= size)
            return -EINVAL;
        offset += URLlength + 1;
        size -= URLlength + 1;
    }

    if (OCRstreamFlag) {
        if (size < 2)
            return -EINVAL;
        offset += 2;
        size -= 2;

        if ((offset >= size || mData[offset] != kTag_DecoderConfigDescriptor)
                && offset - 2 < size
                && mData[offset - 2] == kTag_DecoderConfigDescriptor) {
            // Content found "in the wild" had OCRstreamFlag set but was
            // missing OCR_ES_Id, the decoder config descriptor immediately
            // followed instead.
            offset -= 2;
            size += 2;

            ALOGW("Found malformed 'esds' atom, ignoring missing OCR_ES_Id.");
        }
    }

    if (offset >= size) {
        return -EINVAL;
    }

    uint8_t tag;
    size_t sub_offset, sub_size;
    int err = skipDescriptorHeader(
            offset, size, &tag, &sub_offset, &sub_size);

    if (err != 0) {
        return err;
    }

    if (tag != kTag_DecoderConfigDescriptor) {
        return -EINVAL;
    }

    err = parseDecoderConfigDescriptor(sub_offset, sub_size);

    return err;
}

int ESDS::getBitRate(uint32_t *brateMax, uint32_t *brateAvg) const {
    if (mInitCheck != 0) {
        return mInitCheck;
    }

    *brateMax = mBitRateMax;
    *brateAvg = mBitRateAvg;

    return 0;
};

int ESDS::parseDecoderConfigDescriptor(size_t offset, size_t size) {
    if (size < 13) {
        return -EINVAL;
    }

    mObjectTypeIndication = mData[offset];
    mBitRateMax = U32_AT(mData + offset + 5);
    mBitRateAvg = U32_AT(mData + offset + 9);

    offset += 13;
    size -= 13;

    if (size == 0) {
        mDecoderSpecificOffset = 0;
        mDecoderSpecificLength = 0;
        return 0;
    }

    uint8_t tag;
    size_t sub_offset, sub_size;
    int err = skipDescriptorHeader(
            offset, size, &tag, &sub_offset, &sub_size);

    if (err != 0) {
        return err;
    }

    if (tag != kTag_DecoderSpecificInfo) {
        return -EINVAL;
    }

    mDecoderSpecificOffset = sub_offset;
    mDecoderSpecificLength = sub_size;

    return 0;
}

}  // namespace android

