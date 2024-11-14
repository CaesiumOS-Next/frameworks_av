
#include <fcntl.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <queue>
#include <condition_variable>

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#define LOG_TAG "GIRISH:MediaCodecDecoderNdk"
#include <utils/Log.h>
#include <gtest/gtest.h>

constexpr int64_t kCodecBufferTimeoutMicrosec = 500 * 1000;
constexpr int kZeroOffset = 0;
constexpr int kMaxLogMsgLen = 256;

struct DecodingContext {
  bool output_ended = false;
  bool input_ended = false;
  bool errored = false;
  AMediaExtractor* extractor = nullptr;
  AMediaCodec* codec = nullptr;
  std::queue<int32_t> input_buffers;
  std::queue<std::pair<int32_t, AMediaCodecBufferInfo>> output_buffers;
  std::mutex lock;
  std::condition_variable condition;
};

void logFormat(AMediaFormat* format, const char* what) {
    auto media_format_string = AMediaFormat_toString(format);
    ALOGI("%s: %s format [%s]", __func__, what, media_format_string);
}

void logError(DecodingContext* context,
              media_status_t error, int32_t code,
              const char* detail) {
  ALOGE("%s: Codec Error: %d:%d:%s", __func__, error, code, detail);
  if (context != nullptr) {
    // Notify the error so that it can unblock the waiting thread.
    context->errored = true;
    context->condition.notify_all();
  }
}

enum class BufferState {
  kTryAgainLater,
  kOutputChanged,
  kSuccess,
  kEndOfStream,
  kError,
};

BufferState feedNextSample(AMediaExtractor* extractor, AMediaCodec* codec,
                           int32_t input_buffer_id,
                           DecodingContext* context) {
  // Populate the input buffer with a sample from the media extractor.
  size_t buffer_size = 0;
  uint8_t* input_buffer = AMediaCodec_getInputBuffer(
      codec, input_buffer_id, &buffer_size);

  if (input_buffer == nullptr || buffer_size <= 0) {
    char buffer[kMaxLogMsgLen];
    snprintf(buffer, kMaxLogMsgLen, "%s: Failed to get input buffer on index: %d",
             __func__, input_buffer_id);
    logError(context, AMEDIA_ERROR_INVALID_OPERATION, 0, buffer);
    return BufferState::kError;
  }

  int64_t sample_pts_us =
      AMediaExtractor_getSampleTime(extractor);
  uint32_t flags = AMediaExtractor_getSampleFlags(extractor);
  if (flags == AMEDIA_ERROR_MALFORMED) {
    char buffer[kMaxLogMsgLen];
    snprintf(buffer, kMaxLogMsgLen, "%s: Extractor read Mulformed sample", __func__);
    logError(context, AMEDIA_ERROR_INVALID_OPERATION, 0, buffer);
    return BufferState::kError;
  }

  bool saw_input_eos = false;
  auto bytes_read =
      AMediaExtractor_readSampleData(extractor, input_buffer, buffer_size);
  if (bytes_read <= 0) {
    saw_input_eos = true;
  }
  // Queue the input buffer.
  auto status = AMediaCodec_queueInputBuffer(
      codec, input_buffer_id, /*offset=*/0, bytes_read,
      sample_pts_us,
      saw_input_eos ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0);

  if (AMEDIA_OK != status) {
    char buffer[kMaxLogMsgLen];
    snprintf(buffer, kMaxLogMsgLen, "%s: Failed to queue input buffer on index: %d",
             __func__, input_buffer_id);
    logError(context, status, 0, buffer);
    return BufferState::kError;
  }
  ALOGI("%s: input_buffer(%d:%zu) is fed with: (%zu bytes for pts: %jd and flag: %d",
        __func__, input_buffer_id, buffer_size, bytes_read, sample_pts_us, flags);

  if (saw_input_eos) {
    ALOGI("%s: End of Input", __func__);
    if (context != nullptr) {
      // Notify the end of input for decoding.
      context->input_ended = true;
      context->condition.notify_all();
    }
    return BufferState::kEndOfStream;
  }

  AMediaExtractor_advance(extractor);
  return BufferState::kSuccess;
}

BufferState readNextOutput(AMediaCodec* codec, int32_t output_buffer_id,
                           AMediaCodecBufferInfo* buffer_info,
                           DecodingContext* context) {
  size_t buffer_size_bytes = 0;
  uint8_t* output_buffer_ptr =
      AMediaCodec_getOutputBuffer(codec, output_buffer_id, &buffer_size_bytes);

  bool saw_output_eos = false;
  if ((buffer_info->flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) ||
      buffer_size_bytes == 0) {
      saw_output_eos = true;
  }

  if (buffer_size_bytes > 0) {
    // This is where we need to consume the output.
    // For example:
    //
    // auto output_buffer = std::vector<uint8_t>(
    //     output_buffer_ptr, output_buffer_ptr + buffer_size_bytes);
    auto status = AMediaCodec_releaseOutputBuffer(codec, output_buffer_id, /*render=*/false);
    if (AMEDIA_OK != status) {
      char buffer[kMaxLogMsgLen];
      snprintf(buffer, kMaxLogMsgLen, "%s: Failed to release output buffer on index: %d",
          __func__, output_buffer_id);
      logError(context, status, 0, buffer);
      return BufferState::kError;
    }
  }

  if (saw_output_eos) {
    ALOGI("%s: End of Output", __func__);
    if (context != nullptr) {
      // Notify the end of decoding.
      context->output_ended = true;
      context->condition.notify_all();
    }
    return BufferState::kEndOfStream;
  }

  ALOGI("%s: Successfully decoded frame.", __func__);
  return BufferState::kSuccess;
}

// Thread loop that feeds input into the codec.
// Used only when the codec is configured asynchronously.
void InputLoop(DecodingContext* context) {
  // Loop until input feeding is done or errored out.
  while (!context->input_ended && !context->errored) {
    // Attempt to find an available input buffer to load the sample onto.
    int32_t input_buffer_index = -1;
    {
      std::unique_lock<std::mutex> lock(context->lock);
      context->condition.wait(lock, [context] {
        return context->input_ended || context->errored || !context->input_buffers.empty();
      });

      if (!context->input_buffers.empty()) {
        input_buffer_index = context->input_buffers.front();
        context->input_buffers.pop();
      }
    }

    if (input_buffer_index != -1) {
      feedNextSample(context->extractor, context->codec, input_buffer_index, context);
    }
  }

  ALOGI("%s: InputLoop ending...", __func__);
}

// Thread loop that reads the output from the codec.
// Used only when the codec is configured asynchronously.
void OutputLoop(DecodingContext* context) {
  // Loop until all the outputs are consumed or errored out.
  while (!context->output_ended && !context->errored) {
    // Attempt to find an available output buffer to process it.
    int32_t output_buffer_index = -1;
    AMediaCodecBufferInfo buffer_info;
    {
      std::unique_lock<std::mutex> lock(context->lock);
      context->condition.wait(lock, [context] {
        return context->output_ended || context->errored || !context->output_buffers.empty();
      });

      if (!context->output_buffers.empty()) {
        const std::pair<int32_t, AMediaCodecBufferInfo>& output_buffer_info =
            context->output_buffers.front();
        output_buffer_index = output_buffer_info.first;
        buffer_info = output_buffer_info.second;
        context->output_buffers.pop();
      }
    }

    if (output_buffer_index != -1) {
      readNextOutput(context->codec, output_buffer_index, &buffer_info, context);
    }
  }

  ALOGI("%s: OutputLoop ending...", __func__);
}

// Used when the codec is configured synchronously.
// It will get the next input buffer available, and feeds a sample
// from the extractor.
BufferState ProcessNextSample(AMediaExtractor* extractor, AMediaCodec* codec) {
  // Get index of next input buffer.
  ssize_t input_buffer_id = AMediaCodec_dequeueInputBuffer(codec, 0);
  if (input_buffer_id == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
    ALOGI("%s: Timed out dequeuing input buffer.", __func__);
    return BufferState::kTryAgainLater;
  } else if (input_buffer_id < 0) {
    char buffer[kMaxLogMsgLen];
    snprintf(buffer, kMaxLogMsgLen, "%s: Failed to get input buffer from the codecd", __func__);
    logError(nullptr, static_cast<media_status_t>(input_buffer_id), 0, buffer);
    return BufferState::kError;
  }

  return feedNextSample(extractor, codec, input_buffer_id, nullptr);
}

// Used when the codec is configured synchronously.
// It will read the output from the next output buffer available.
BufferState ProcessNextOutputBuffer(AMediaCodec* codec) {
  AMediaCodecBufferInfo buffer_info;
  ssize_t output_buffer_id = AMediaCodec_dequeueOutputBuffer(
      codec, &buffer_info, kCodecBufferTimeoutMicrosec);
  ALOGI("%s: output_buffer_id: %zd", __func__, output_buffer_id);
  if (output_buffer_id == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
    ALOGI("%s: Timed out dequeuing output buffer.", __func__);
    return BufferState::kTryAgainLater;
  } else if (output_buffer_id == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
    ALOGI("%s: AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED", __func__);
    return BufferState::kOutputChanged;
  }

  return readNextOutput(codec, output_buffer_id, &buffer_info, nullptr);
}

// Start Video codec fro the video track in the extractor.
// If a valid context is passed, the codec will be configured to
// run in asynchronous mode.
AMediaCodec* StartVideoCodec(AMediaExtractor* extractor,
                             DecodingContext* context,
                             AMediaCodecOnAsyncNotifyCallback& decoder_callbacks) {
  const int number_of_tracks = AMediaExtractor_getTrackCount(extractor);
  ALOGI("%s: Number of tracks: %d", __func__, number_of_tracks);
  if (number_of_tracks < 1) {
    ALOGE("%s: Require at least one video track.", __func__);
    return nullptr;
  }

  for (int i = 0; i < number_of_tracks; ++i) {
    AMediaFormat* track_format = AMediaExtractor_getTrackFormat(extractor, i);
    if (track_format == nullptr) {
      ALOGW("%s: Track %d has no media format.", __func__, i);
      continue;
    }

    const char* mime_type_raw = nullptr;
    if (!AMediaFormat_getString(track_format, AMEDIAFORMAT_KEY_MIME,
                                &mime_type_raw)) {
      AMediaFormat_delete(track_format);
      ALOGE("%s: Mime type not found.", __func__);
      return nullptr;
    }
    std::string mime_type = std::string(mime_type_raw);
    if (mime_type.find("video/") != 0) {
      AMediaFormat_delete(track_format);
      continue;
    }

    logFormat(track_format, "Extractor Video track");

    int max_input_size = -1;
    AMediaFormat_getInt32(track_format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE,
                          &max_input_size);
    int width = 0;
    int height = 0;
    AMediaFormat_getInt32(track_format, AMEDIAFORMAT_KEY_WIDTH, &width);
    AMediaFormat_getInt32(track_format, AMEDIAFORMAT_KEY_HEIGHT, &height);

    // Check if we need to increase the input buffer size.
    int32_t resolution = width * height;
    if (resolution > max_input_size) {
      AMediaFormat_setInt32(track_format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, resolution);
      max_input_size = resolution;
    }
    ALOGI("%s: Max input buffer size: %d", __func__, max_input_size);

    AMediaFormat_setInt32(track_format, AMEDIAFORMAT_KEY_CAPTURE_RATE, 24);
    AMediaFormat_setInt32(track_format, AMEDIAFORMAT_KEY_PUSH_BLANK_BUFFERS_ON_STOP, 0);

    // Select the video track.
    ALOGI("%s: Selecting video track: %d", __func__, i);
    if (AMEDIA_OK != AMediaExtractor_selectTrack(extractor, i)) {
      ALOGE("%s: Failed to select the video track!", __func__);
      AMediaFormat_delete(track_format);
      return nullptr;
    }

    // Create the codec.
    AMediaCodec* codec = AMediaCodec_createDecoderByType(mime_type_raw);
    if (codec == nullptr) {
      ALOGE("%s: Unable to create AMediaCodec.", __func__);
      AMediaFormat_delete(track_format);
      return nullptr;
    }

    char* codecName = nullptr;
    AMediaCodec_getName(codec, &codecName);
    ALOGI("%s: Codec Name is: %s", __func__, codecName);
    AMediaCodec_releaseName(codec, codecName);

    if (context != nullptr) {
      if (AMEDIA_OK != AMediaCodec_setAsyncNotifyCallback(
          codec, decoder_callbacks, context)) {
        ALOGE("%s: AMediaCodec_setAsyncNotifyCallback failed", __func__);
        AMediaFormat_delete(track_format);
        AMediaCodec_delete(codec);
        return nullptr;
      }
      context->extractor = extractor;
      context->codec = codec;
    }

    if (AMEDIA_OK != AMediaCodec_configure(codec, track_format,
                                           /*window=*/nullptr, /*cypto=*/nullptr,
                                           /*flags=*/0)) {
      ALOGE("%s: Failed to configure the video codec", __func__);
      AMediaFormat_delete(track_format);
      AMediaCodec_delete(codec);
      return nullptr;
    }

    if (AMEDIA_OK != AMediaCodec_start(codec)) {
      ALOGE("%s: Failed to start the video codec", __func__);
      AMediaFormat_delete(track_format);
      AMediaCodec_delete(codec);
      return nullptr;
    }

    AMediaFormat_delete(track_format);
    return codec;
  }

  ALOGE("%s: No video track found.", __func__);
  return nullptr;
}

inline size_t getFileSize(const int fd) {
    struct stat buf;
    int32_t status = fstat(fd, &buf);
    return buf.st_size;
}

inline size_t getFileSizeUsingSeek(const int fd) {
  auto file_size = lseek(fd, kZeroOffset, SEEK_END);
  lseek(fd, kZeroOffset, SEEK_SET);
  return file_size;
}

// Set up the extractor for the given media file.
AMediaExtractor* GetAndroidMediaExtractor(const std::string& file_path) {
  ALOGI("%s: GetAndroidMediaExtractor: %s", __func__, file_path.c_str());
  // Need to use raw file descriptor because Media NDK API needs it.
  const int fd = open(file_path.data(), O_RDONLY);
  if (fd == -1) {
    ALOGE("%s: Unable to open file %s", __func__, file_path.c_str());
    return nullptr;
  }

  // File size is needed by the Media Extractor below.
  auto file_size = getFileSize(fd);
  ALOGI("%s: Opened file descriptor %d with length: %zu", __func__, fd, file_size);

  // Create the Media Extractor and set the FD as its source.
  AMediaExtractor* extractor = AMediaExtractor_new();
  if (extractor == nullptr) {
    ALOGE("%s: Unable to create AMediaExtractor.", __func__);
    close(fd);
    return nullptr;
  }

  if (AMEDIA_OK != AMediaExtractor_setDataSourceFd(
      extractor, fd, kZeroOffset, file_size)) {
    ALOGE("%s: Failed to set data source to extractor!", __func__);
    AMediaExtractor_delete(extractor);
    close(fd);
    return nullptr;
  }

  close(fd);
  return extractor;
}

// Run decoder on the video track of the given media file.
int RunMediaDecoder(const std::string& filename, bool async) {
  ALOGI("Running decoder %s.", async ? "asynchronously" : "synchronously");
  AMediaExtractor* extractor = GetAndroidMediaExtractor(filename);
  if (extractor == nullptr) {
    ALOGI("%s: Failed to open extractor", __func__);
    return -1;
  }

  // Setup decoder callbacks.
  AMediaCodecOnAsyncNotifyCallback decoder_callbacks;

  decoder_callbacks.onAsyncInputAvailable = [](AMediaCodec* codec,
                                               void* userdata, int32_t index) {
    (void)codec;
    DecodingContext* context = reinterpret_cast<DecodingContext*>(userdata);
    ALOGI("%s: input index: %d", __func__, index);
    {
      std::unique_lock lk(context->lock);
      context->input_buffers.push(index);
      context->condition.notify_one();
    }
  };

  decoder_callbacks.onAsyncOutputAvailable =
      [](AMediaCodec* codec, void* userdata, int32_t index,
         AMediaCodecBufferInfo* buffer_info) {
        (void)codec;
        ALOGI("%s: output index: %d", __func__, index);
        DecodingContext* context = reinterpret_cast<DecodingContext*>(userdata);
        {
          std::unique_lock lk(context->lock);
          context->output_buffers.push(std::make_pair(index, *buffer_info));
          context->condition.notify_one();
          //readNextOutput(codec, index, buffer_info, context);
        }
      };

  decoder_callbacks.onAsyncFormatChanged =
      [](AMediaCodec* codec, void* userdata, AMediaFormat* format) {
        (void)userdata;
        (void)codec;
        logFormat(format, "Decoder Output");
      };

  decoder_callbacks.onAsyncError = [](AMediaCodec* codec, void* userdata,
                                      media_status_t error, int32_t code,
                                      const char* detail) {
    (void)codec;
    DecodingContext* context = reinterpret_cast<DecodingContext*>(userdata);
    logError(context, error, code, detail);
  };

  std::unique_ptr<DecodingContext> context = async ? std::make_unique<DecodingContext>() : nullptr;
  AMediaCodec* codec = StartVideoCodec(extractor, context.get(), decoder_callbacks);
  if (codec == nullptr) {
    AMediaExtractor_delete(extractor);
    ALOGI("%s: Failed to open codec", __func__);
    return -1;
  }

  if (async) {
    // Codec is configured to work asynchronously.
    // So, start input and output threads.
    auto input_thread = std::thread(InputLoop, context.get());
    auto output_thread = std::thread(OutputLoop, context.get());
    ALOGI("%s: Waiting for Decoding operation asynchronously", __func__);

    // Wait for Input and Output threads, if they are still running.
    if (input_thread.joinable()) {
      input_thread.join();
    }
    if (output_thread.joinable()) {
      output_thread.join();
    }
  } else {
    // Codec is configured to work synchronously.
    // So, start feeding the input and process the output.
    for (;;) {
      // Keep processing input buffers until we run out of data or available
      // buffers.
      for (;;) {
        BufferState input_buffer_state = ProcessNextSample(extractor, codec);
        if (input_buffer_state != BufferState::kSuccess) {
          break;
        }
      }
      // Keep processing output buffers until we run out of available buffers.
      for (;;) {
        BufferState output_buffer_state = ProcessNextOutputBuffer(codec);
        if (output_buffer_state != BufferState::kSuccess) {
          break;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  ALOGI("%s: Stopping codec.", __func__);
  AMediaExtractor_delete(extractor);
  AMediaCodec_stop(codec);
  AMediaCodec_delete(codec);

  return 0;
}

/*
 *
 * Test setup:
 * ==========
 * The test reads the input media from /data/local/tmp/MediaBenchmark/res/
 * So, before running this test, make sure that the test contents are copied to
 * the device.
 * The target media file is available @
 *  frameworks/av/media/module/libmediatranscoding/tests/assets/TranscodingTestAssets/
 * For example:
 * adb shell mkdir -p /data/local/tmp/MediaBenchmark/res/
 * adb push \
 *  frameworks/av/media/module/libmediatranscoding/tests/assets/TranscodingTestAssets/backyard_hevc_1920x1080_20Mbps.mp4 \
 *  /data/local/tmp/MediaBenchmark/res/
 *
 *
 * To build the test:
 * =================
 * make libmediandk_test -j
 * adb shell mkdir -p /data/nativetest64/libmediandk_test/
 * adb push $OUT/data/nativetest64/libmediandk_test/libmediandk_test \
 *          /data/nativetest64/libmediandk_test/
 *
 * To run the test:
 * ================
 * - adb shell "logcat -c; ./data/nativetest64/libmediandk_test/libmediandk_test"
 *
 * To capture the test (specific) logs:
 * ====================================
 * - adb shell "logcat -vtime" | grep MediaCodecDecoderNdk
 *
 */
TEST(NdkMediaCodec_tests, test_codec_sync) {
  std::string filename = "/data/local/tmp/MediaBenchmark/res/backyard_hevc_1920x1080_20Mbps.mp4";
  ALOGI("%s: filename: %s", __func__, filename.c_str());
  RunMediaDecoder(filename, false);
}

TEST(NdkMediaCodec_tests, test_codec_async) {
  std::string filename = "/data/local/tmp/MediaBenchmark/res/backyard_hevc_1920x1080_20Mbps.mp4";
  ALOGI("%s: filename: %s", __func__, filename.c_str());
  RunMediaDecoder(filename, true);
}

