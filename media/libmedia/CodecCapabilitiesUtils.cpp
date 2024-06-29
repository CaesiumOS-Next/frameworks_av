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
#define LOG_TAG "CodecCapabilitiesUtils"

#include <android-base/properties.h>
#include <utils/Log.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <media/CodecCapabilitiesUtils.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AUtils.h>

namespace android {

// VideoSize

VideoSize::VideoSize(int32_t width, int32_t height) : mWidth(width), mHeight(height) {}

VideoSize::VideoSize() : mWidth(0), mHeight(0) {}

int32_t VideoSize::getWidth() const { return mWidth; }

int32_t VideoSize::getHeight() const { return mHeight; }

bool VideoSize::equals(VideoSize other) const {
    return mWidth == other.mWidth && mHeight == other.mHeight;
}

std::string VideoSize::toString() const {
    return std::to_string(mWidth) + "x" + std::to_string(mHeight);
}

std::optional<VideoSize> VideoSize::ParseSize(std::string str) {
    if (str.empty()) {
        return std::nullopt;
    }

    std::regex regex("([0-9]+)([*x])([0-9]+)");
    std::smatch match;
    if (std::regex_match(str, match, regex)) {
        int w = std::atoi(match[1].str().c_str());
        int h = std::atoi(match[3].str().c_str());
        return std::make_optional(VideoSize(w, h));
    } else {
        ALOGW("could not parse size %s", str.c_str());
        return std::nullopt;
    }
}

bool VideoSize::empty() const {
    return mWidth <= 0 || mHeight <= 0;
}

// Utils functions

std::optional<Rational> ParseRational(std::string str) {
    if (str.compare("NaN") == 0) {
        return std::make_optional(NaN);
    } else if (str.compare("Infinity") == 0) {
        return std::make_optional(POSITIVE_INFINITY);
    } else if (str.compare("-Infinity") == 0) {
        return std::make_optional(NEGATIVE_INFINITY);
    }

    std::regex regex("([0-9]+)([:/])([0-9]+)");
    std::smatch match;
    if (std::regex_match(str, match, regex)) {
        int numerator = std::atoi(match[1].str().c_str());
        int denominator = std::atoi(match[3].str().c_str());
        return std::make_optional(Rational(numerator, denominator));
    } else {
        ALOGW("could not parse string: %s to Rational", str.c_str());
        return std::nullopt;
    }
}

Range<int32_t> FactorRange(Range<int32_t> range, int32_t factor) {
    if (factor == 1) {
        return range;
    }
    return Range(divUp(range.lower(), factor), range.upper() / factor);
}

Range<int64_t> FactorRange(Range<int64_t> range, int64_t factor) {
    if (factor == 1) {
        return range;
    }
    return Range(divUp(range.lower(), factor), range.upper() / factor);
}

Rational ScaleRatio(Rational ratio, int num, int den) {
    int common = std::gcd(num, den);
    num /= common;
    den /= common;
    return Rational(
            (int)(ratio.getNumerator() * (double)num),     // saturate to int
            (int)(ratio.getDenominator() * (double)den));  // saturate to int
}

Range<Rational> ScaleRange(Range<Rational> range, int num, int den) {
    if (num == den) {
        return range;
    }
    return Range(
            ScaleRatio(range.lower(), num, den),
            ScaleRatio(range.upper(), num, den));
}

Range<int> IntRangeFor(double v) {
    return Range((int)v, (int)ceil(v));
}

Range<int64_t> Int64RangeFor(double v) {
    return Range((int64_t)v, (int64_t)ceil(v));
}

Range<int> AlignRange(Range<int> range, int align) {
    return range.intersect(
            divUp(range.lower(), align) * align,
            (range.upper() / align) * align);
}

// parse string into int range
std::optional<Range<int>> ParseIntRange(const std::string &str) {
    if (str.empty()) {
        ALOGW("could not parse empty integer range");
        return std::nullopt;
    }
    int lower, upper;
    std::regex regex("([0-9]+)-([0-9]+)");
    std::smatch match;
    if (std::regex_match(str, match, regex)) {
        lower = std::atoi(match[1].str().c_str());
        upper = std::atoi(match[2].str().c_str());
    } else if (std::atoi(str.c_str()) != 0) {
        lower = upper = std::atoi(str.c_str());
    } else {
        ALOGW("could not parse integer range: %s", str.c_str());
        return std::nullopt;
    }
    return std::make_optional<Range<int>>(lower, upper);
}

std::optional<Range<int64_t>> ParseInt64Range(const std::string str) {
    if (str.empty()) {
        ALOGW("could not parse empty int64 range");
        return std::nullopt;
    }
    int64_t lower, upper;
    std::regex regex("([0-9]+)-([0-9]+)");
    std::smatch match;
    if (std::regex_match(str, match, regex)) {
        lower = std::atol(match[1].str().c_str());
        upper = std::atol(match[2].str().c_str());
    } else if (std::atol(str.c_str()) != 0) {
        lower = upper = std::atoi(str.c_str());
    } else {
        ALOGW("could not parse int64 range: %s", str.c_str());
        return std::nullopt;
    }
    return std::make_optional<Range<int64_t>>(lower, upper);
}

std::optional<Range<Rational>> ParseRationalRange(const std::string str) {
    size_t ix = str.find_first_of('-');
    if (ix != std::string::npos) {
        std::optional<Rational> lower = ParseRational(str.substr(0, ix));
        std::optional<Rational> upper = ParseRational(str.substr(ix + 1));
        if (!lower || !upper) {
            return std::nullopt;
        }
        return std::make_optional<Range<Rational>>(lower.value(), upper.value());
    } else {
        std::optional<Rational> value = ParseRational(str);
        if (!value) {
            return std::nullopt;
        }
        return std::make_optional<Range<Rational>>(value.value(), value.value());
    }
}

std::optional<std::pair<VideoSize, VideoSize>> ParseSizeRange(const std::string str) {
    size_t ix = str.find_first_of('-');
    if (ix != std::string::npos) {
        std::optional<VideoSize> lowerOpt = VideoSize::ParseSize(str.substr(0, ix));
        std::optional<VideoSize> upperOpt = VideoSize::ParseSize(str.substr(ix + 1));
        if (!lowerOpt || !upperOpt) {
            return std::nullopt;
        }
        return std::make_optional(
                std::pair<VideoSize, VideoSize>(lowerOpt.value(), upperOpt.value()));
    } else {
        std::optional<VideoSize> opt = VideoSize::ParseSize(str);
        if (!opt) {
            return std::nullopt;
        }
        return std::make_optional(std::pair<VideoSize, VideoSize>(opt.value(), opt.value()));
    }
}

Range<int32_t> GetSizeRange() {
#ifdef __LP64__
    return Range<int32_t>(1, 32768);
#else
    int32_t value = base::GetIntProperty("media.resolution.limit.32bit", (int32_t)4096);
    return Range<int32_t>(1, value);
#endif
}

int64_t divUpInt64(int64_t num, int64_t den) {
    return (num + den - 1) / den;
}

bool IsPowerOfTwo(int32_t value) {
    return (value & (value - 1)) == 0;
}

}  // namespace android