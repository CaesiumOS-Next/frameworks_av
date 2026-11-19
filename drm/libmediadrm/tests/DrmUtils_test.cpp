/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include <gtest/gtest.h>

#include <sstream>

#include <mediadrm/DrmUtils.h>

namespace android {
namespace DrmUtils {
namespace {

constexpr uint8_t kClearKey[16] = {0xe2, 0x71, 0x9d, 0x58, 0xa9, 0x85, 0xb3, 0xc9,
                                   0x78, 0x1a, 0xb0, 0x30, 0xaf, 0x78, 0xd3, 0x0e};
constexpr uint8_t kOther[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

std::map<std::string, std::vector<Uuid>> parse(const std::string& json) {
    std::istringstream in(json);
    return parseUuidMap(in, "test");
}

TEST(DrmUtilsUuidMapTest, ParsesInstancesAndUuids) {
    auto map = parse(R"({
        "clearkey": ["1077efec-c0b2-4d02-ace3-3c1e52e2fb4b",
                     "e2719d58-a985-b3c9-781a-b030af78d30e"],
        "hdcp2": ["32f62de0-131d-4b51-ad51-7aa58ce434f4"]
    })");
    ASSERT_EQ(2u, map.size());
    ASSERT_EQ(2u, map["clearkey"].size());
    ASSERT_EQ(1u, map["hdcp2"].size());
    EXPECT_EQ(0x10, map["clearkey"][0].uuid[0]);
    EXPECT_EQ(0x77, map["clearkey"][0].uuid[1]);
    EXPECT_EQ(0x4b, map["clearkey"][0].uuid[15]);
}

TEST(DrmUtilsUuidMapTest, AcceptsUuidWithoutDashes) {
    auto map = parse(R"({"a": ["e2719d58a985b3c9781ab030af78d30e"]})");
    ASSERT_EQ(1u, map["a"].size());
    EXPECT_EQ(0, memcmp(map["a"][0].uuid.data(), kClearKey, 16));
}

TEST(DrmUtilsUuidMapTest, EmptyDocumentGivesEmptyMap) {
    EXPECT_TRUE(parse("").empty());
    EXPECT_TRUE(parse("{}").empty());
}

TEST(DrmUtilsUuidMapTest, MalformedDocumentGivesEmptyMap) {
    EXPECT_TRUE(parse("{not json").empty());
    EXPECT_TRUE(parse("[]").empty());
    EXPECT_TRUE(parse("\"clearkey\"").empty());
}

TEST(DrmUtilsUuidMapTest, DropsOnlyTheBrokenInstance) {
    auto map = parse(R"({
        "good": ["e2719d58-a985-b3c9-781a-b030af78d30e"],
        "notarray": "e2719d58-a985-b3c9-781a-b030af78d30e",
        "short": ["e2719d58"],
        "nonstring": [1234],
        "badhex": ["zz719d58-a985-b3c9-781a-b030af78d30e"],
        "toolong": ["e2719d58-a985-b3c9-781a-b030af78d30e00"]
    })");
    EXPECT_EQ(1u, map.size());
    EXPECT_EQ(1u, map.count("good"));
}

TEST(DrmUtilsUuidMapTest, EmptyUuidListIsKept) {
    auto map = parse(R"({"none": []})");
    ASSERT_EQ(1u, map.count("none"));
    EXPECT_TRUE(map["none"].empty());
}

TEST(DrmFactoryAidlTest, UnknownUuidsMayHandleAnything) {
    DrmFactoryAidl factory("x", std::nullopt);
    EXPECT_TRUE(factory.mayHandle(kClearKey));
    EXPECT_TRUE(factory.mayHandle(kOther));
    EXPECT_FALSE(factory.knownUuids().has_value());
}

TEST(DrmFactoryAidlTest, KnownUuidsFilterByMatch) {
    DrmFactoryAidl factory("x", std::vector<Uuid>{toAidlUuid(kClearKey)});
    EXPECT_TRUE(factory.mayHandle(kClearKey));
    EXPECT_FALSE(factory.mayHandle(kOther));
    ASSERT_TRUE(factory.knownUuids().has_value());
    EXPECT_EQ(1u, factory.knownUuids()->size());
}

TEST(DrmFactoryAidlTest, EmptyKnownUuidsHandlesNothing) {
    DrmFactoryAidl factory("x", std::vector<Uuid>{});
    EXPECT_FALSE(factory.mayHandle(kClearKey));
}

}  // namespace
}  // namespace DrmUtils
}  // namespace android
