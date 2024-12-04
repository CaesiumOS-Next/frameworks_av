/*
 * Copyright 2024 The Android Open Source Project
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
#define LOG_TAG "NdkMediaCodecStoreTest"

#include <gtest/gtest.h>
#include <utils/Log.h>

#include <media/NdkMediaCodecStore.h>

namespace android {
    class NdkMediaCodecStoreTest : public ::testing::Test {
    };

    TEST(NdkMediaCodecStoreTest, decoderTest) {
        // get all the supported media types
        const AMediaCodecSupportedMediaType *mediaTypes;
        size_t count;
        media_status_t err = AMediaCodecStore_getSupportedMediaTypes(&mediaTypes, &count);
        ASSERT_EQ(err, AMEDIA_OK) << "getSupportedMediaTypes failed";

        // pick the first supported decoder type
        const char *decoderType;
        for (int i = 0; i < count; i++) {
            if((mediaTypes[i].mMode & AMediaCodecSupportedMediaType::FLAG_DECODER) > 0) {
                decoderType = mediaTypes[i].mMediaType;
                break;
            }
        }
        ASSERT_NE(decoderType, nullptr) << "No decoder media type found";

        // build AMeidaFormat with the media type
        AMediaFormat *fmt = AMediaFormat_new();
        AMediaFormat_setString(fmt, "mime", decoderType);

        // test findNextDecoderForFormat()
        const AMediaCodecInfo *codecInfo;
        for (;;) {
            err = AMediaCodecStore_findNextDecoderForFormat(fmt, &codecInfo);
            if (codecInfo == nullptr) {
                EXPECT_EQ(err, AMEDIA_ERROR_UNSUPPORTED);
                break;
            }

            // test getMediaType
            EXPECT_EQ(err, AMEDIA_OK);
            EXPECT_EQ(AMediaCodecInfo_getMediaType(codecInfo), decoderType);

            // test AAudioCapabilities
            const ACodecAudioCapabilities *audioCaps;
            err = AMediaCodecInfo_getAudioCapabilities(codecInfo, &audioCaps);
            if (err == AMEDIA_OK) {
                ASSERT_NE(audioCaps, nullptr);
                // test getBitrateRange
                AIntRange *range = new AIntRange;
                err = ACodecAudioCapabilities_getBitrateRange(audioCaps, range);
                EXPECT_NE(range, nullptr);
                delete range;
            }

            // test AVideoCapabilities
            const ACodecVideoCapabilities *videoCaps;
            err = AMediaCodecInfo_getVideoCapabilities(codecInfo, &videoCaps);
            if (err == AMEDIA_OK) {
                ASSERT_NE(videoCaps, nullptr);
                // test getSupportedPerformancePoints
                const ACodecPerformancePoint *pp;
                size_t ppCount;
                err = ACodecVideoCapabilities_getSupportedPerformancePoints(
                        videoCaps, &pp, &ppCount);
                if (err == AMEDIA_OK) {
                    EXPECT_NE(pp, nullptr);
                    EXPECT_NE(ppCount, 0);
                }
                // test getHeightAlignment
                int32_t heightAlignment
                        = ACodecVideoCapabilities_getHeightAlignment(videoCaps);
                EXPECT_NE(heightAlignment, 0);
            }

            // test AEncoderCapabilities
            const ACodecEncoderCapabilities *encoderCaps;
            err = AMediaCodecInfo_getEncoderCapabilities(codecInfo, &encoderCaps);
            if (err == AMEDIA_OK) {
                ASSERT_NE(encoderCaps, nullptr);
                // test isBitrateModeSupported
                EXPECT_EQ(ACodecEncoderCapabilities_isBitrateModeSupported(encoderCaps,
                        BITRATE_MODE_CBR), 0);
            }
        }

        AMediaFormat_delete(fmt);
    }

    TEST(NdkMediaCodecStoreTest, performancePointTest) {
        ACodecPerformancePoint *pp1 = ACodecPerformancePoint_create(1280, 720, 240);
        ASSERT_NE(pp1, nullptr);
        ACodecPerformancePoint *pp2 = ACodecPerformancePoint_create(3840, 2160, 120);
        ASSERT_NE(pp2, nullptr);

        EXPECT_EQ(ACodecPerformancePoint_covers(pp1, pp2), false);

        ACodecPerformancePoint_delete(pp1);
        ACodecPerformancePoint_delete(pp2);
    }
}  // namespace android