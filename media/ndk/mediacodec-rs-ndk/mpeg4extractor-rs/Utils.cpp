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

#include "mpeg4extractor/Utils.h"

#include <log/log.h>

#include <sstream>

std::string StringPrintf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);

    char *buffer;
    vasprintf(&buffer, format, ap);
    va_end(ap);

    std::string s = buffer;
    free(buffer);
    buffer = nullptr;

    return s;
}

void hexdump(const void* _data, size_t size) {
    auto* data = static_cast<const uint8_t*>(_data);

    std::stringstream ss;
    for (size_t i = 0; i < size; ++i) {
        auto column = i % 16;

        if (column == 0) {
            ss << StringPrintf("%08zx: ", i);
        }

        ss << StringPrintf("%02x ", data[i]);

        if (column == 7) {
            ss << " ";
        }

        if ((column + 1) == 16 || (i + 1) == size) {
            auto line = ss.str();
            ALOG(LOG_INFO, "hexdump", "%s", line.c_str());
            ss = std::stringstream();
        }
    }
}
