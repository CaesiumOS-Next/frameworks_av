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
#define LOG_TAG "Codec2-InputSurface"
#include <android-base/logging.h>

#include <C2Config.h>

#include <codec2/aidl/inputsurface/InputSurface.h>


namespace aidl::android::hardware::media::c2::utils {

template <typename T>
static C2R BasicSetter(bool, C2InterfaceHelper::C2P<T> &) {
    return C2R::Ok();
}

// Derived class of C2InterfaceHelper
class InputSurface::Interface : public C2InterfaceHelper {
public:
    // A missing C2Config string definition
    constexpr static char C2_PARAMKEY_INPUT_SURFACE_CURRENT_WORK_STATUS[] =
            "input-surface.current-work-status";

    // Default image size for AImageReader
    constexpr static uint32_t kDefaultImageWidth = 1280;
    constexpr static uint32_t kDefaultImageHeight = 720;

    // Deafult # of buffers for AImageReader
    constexpr static  uint32_t kDefaultImageBufferCount = 16;

    // Config for AImageReader creation
    struct ImageConfig {
        int32_t mWidth;         // image width
        int32_t mHeight;        // image height
        int32_t mFormat;        // image pixel format
        int32_t mNumBuffers;    // number of max images for AImageReader(consumer)
        uint64_t mUsage;        // image usage
    };

    // Config for InputSurface active buffer stream control
    struct StreamConfig {
        // IN PARAMS
        float mMinFps = 0.0;        // minimum fps (repeat frame to achieve this)
        float mMaxFps = 0.0;        // max fps (via frame drop)
        float mCaptureFps = 0.0;    // capture fps
        float mCodedFps = 0.0;      // coded fps
        bool mSuspended = false;    // suspended
        int64_t mTimeOffsetUs = 0;  // time offset (input => codec)
        int64_t mSuspendAtUs = 0;   // suspend/resume time
        int64_t mStartAtUs = 0;     // start time
        bool mStopped = false;      // stopped
        int64_t mStopAtUs = 0;      // stop time

        // OUT PARAMS
        int64_t mInputDelayUs = 0;  // delay between encoder input and surface input

        // IN PARAMS (CODEC WRAPPER)
        float mFixedAdjustedFps = 0.0;  // fixed fps via PTS manipulation
        float mMinAdjustedFps = 0.0;    // minimum fps via PTS manipulation
        int mPriority = INT_MAX;        // priority of queue thread (if any);
                                        // INT_MAX for no-op
    };

    // TODO: remove this
    // Config for current work status w.r.t input buffers
    struct WorkStatusConfig {
        int64_t mLastDoneIndex = -1;    // Last work done input buffer index
        uint64_t mEmptiedCount = 0;     // # of input buffers being emptied
    };

    explicit Interface(
            const std::shared_ptr<C2ReflectorHelper> &helper)
        : C2InterfaceHelper(helper) {

        setDerivedInstance(this);

        addParameter(
                DefineParam(mBlockSize, C2_PARAMKEY_BLOCK_SIZE)
                .withDefault(new C2StreamBlockSizeInfo::output(
                        0u, kDefaultImageWidth, kDefaultImageHeight))
                .withFields({
                        C2F(mBlockSize, width).inRange(2, 8192, 2),
                        C2F(mBlockSize, height).inRange(2, 8192, 2),})
                .withSetter(BlockSizeSetter)
                .build());
        addParameter(
                DefineParam(mBlockCount, C2_PARAMKEY_BLOCK_COUNT)
                .withDefault(new C2StreamBlockCountInfo::output(
                    0u, kDefaultImageBufferCount))
                .withFields({C2F(mBlockCount, value).any()})
                .withSetter(BlockCountSetter)
                .build());
        addParameter(
                DefineParam(mPixelFormat, C2_PARAMKEY_PIXEL_FORMAT)
                .withDefault(new C2StreamPixelFormatInfo::output(
                        0u, HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED))
                .withFields({C2F(mPixelFormat, value).any()})
                .withSetter(BasicSetter<decltype(mPixelFormat)::element_type>)
                .build());
        addParameter(
                DefineParam(mUsage, C2_PARAMKEY_OUTPUT_STREAM_USAGE)
                .withDefault(new C2StreamUsageTuning::output(0u, 0ULL))
                .withFields({C2F(mUsage, value).any()})
                .withSetter(BasicSetter<decltype(mUsage)::element_type>)
                .build());

        addParameter(
                DefineParam(mMinFps, C2_PARAMKEY_INPUT_SURFACE_MIN_FRAME_RATE)
                .withDefault(new C2PortMinFrameRateTuning::output(0.0))
                .withFields({C2F(mMinFps, value).any()})
                .withSetter(BasicSetter<decltype(mMinFps)::element_type>)
                .build());
        addParameter(
                DefineParam(mMaxFps, C2_PARAMKEY_INPUT_SURFACE_MAX_FRAME_RATE)
                .withDefault(new C2PortMaxFrameRateTuning::output(0.0))
                .withFields({C2F(mMaxFps, value).any()})
                .withSetter(BasicSetter<decltype(mMaxFps)::element_type>)
                .build());
        addParameter(
                DefineParam(mCaptureFps, C2_PARAMKEY_INPUT_SURFACE_CAPTURE_FRAME_RATE)
                .withDefault(new C2PortCaptureFrameRateTuning::output(0.0))
                .withFields({C2F(mCaptureFps, value).any()})
                .withSetter(BasicSetter<decltype(mCaptureFps)::element_type>)
                .build());
        addParameter(
                DefineParam(mCodedFps, C2_PARAMKEY_FRAME_RATE)
                .withDefault(new C2StreamFrameRateInfo::output(0u, 0.0))
                .withFields({C2F(mCodedFps, value).any()})
                .withSetter(BasicSetter<decltype(mCodedFps)::element_type>)
                .build());
        addParameter(
                DefineParam(mTimeOffset, C2_PARAMKEY_FRAME_RATE)
                .withDefault(new C2ComponentTimeOffsetTuning(0ULL))
                .withFields({C2F(mTimeOffset, value).any()})
                .withSetter(BasicSetter<decltype(mTimeOffset)::element_type>)
                .build());
        addParameter(
                DefineParam(mStarted, C2_PARAMKEY_INPUT_SURFACE_START_AT)
                .withDefault(new C2PortStartTimestampTuning::output(0ULL))
                .withFields({
                        C2F(mStarted, enabled).any(),
                        C2F(mStarted, timestamp).any()})
                .withSetter(BasicSetter<decltype(mStarted)::element_type>)
                .build());
        addParameter(
                DefineParam(mStopped, C2_PARAMKEY_INPUT_SURFACE_STOP_AT)
                .withDefault(new C2PortStopTimestampTuning::output())
                .withFields({
                        C2F(mStopped, enabled).any(),
                        C2F(mStopped, timestamp).any()})
                .withSetter(BasicSetter<decltype(mStopped)::element_type>)
                .build());
        addParameter(
                DefineParam(mSuspended, C2_PARAMKEY_INPUT_SURFACE_SUSPEND_AT)
                .withDefault(new C2PortSuspendTimestampTuning::output())
                .withFields({
                        C2F(mSuspended, enabled).any(),
                        C2F(mSuspended, timestamp).any()})
                .withSetter(BasicSetter<decltype(mSuspended)::element_type>)
                .build());
        addParameter(
                DefineParam(mResumed, C2_PARAMKEY_INPUT_SURFACE_RESUME_AT)
                .withDefault(new C2PortResumeTimestampTuning::output(0ULL))
                .withFields({
                        C2F(mResumed, enabled).any(),
                        C2F(mResumed, timestamp).any()})
                .withSetter(BasicSetter<decltype(mResumed)::element_type>)
                .build());
        addParameter(
                DefineParam(mGap, C2_PARAMKEY_INPUT_SURFACE_TIMESTAMP_ADJUSTMENT)
                .withDefault(new C2PortTimestampGapTuning::output(
                        C2TimestampGapAdjustmentStruct::NONE, 0ULL))
                .withFields({
                        C2F(mGap, mode)
                                .oneOf({
                                        C2TimestampGapAdjustmentStruct::NONE,
                                        C2TimestampGapAdjustmentStruct::MIN_GAP,
                                        C2TimestampGapAdjustmentStruct::FIXED_GAP}),
                        C2F(mGap, value).any()})
                .withSetter(BasicSetter<decltype(mGap)::element_type>)
                .build());
        addParameter(
                DefineParam(mStopTimeOffset, C2_PARAMKEY_INPUT_SURFACE_STOP_TIME_OFFSET)
                .withDefault(new C2PortStopTimeOffset::output(0ULL))
                .withFields({C2F(mStopTimeOffset, value).any()})
                .withSetter(BasicSetter<decltype(mStopTimeOffset)::element_type>)
                .build());

        std::shared_ptr<C2CurrentWorkTuning> defaultCurrentWorkTuning =
                std::make_shared<C2CurrentWorkTuning>();
        defaultCurrentWorkTuning->timestamp = 0;
        defaultCurrentWorkTuning->frameIndex = 0;
        defaultCurrentWorkTuning->customOrdinal = 0;

        addParameter(
                DefineParam(mCurrentWorkStatus, C2_PARAMKEY_INPUT_SURFACE_CURRENT_WORK_STATUS)
                .withDefault(defaultCurrentWorkTuning)
                /*
                .withFields({
                        C2F(mCurrentWorkStatus, timestamp).any(),
                        C2F(mCurrentWorkStatus, frameIndex).any(),
                        C2F(mCurrentWorkStatus, customOrdinal).any()})
                        */
                .withSetter(BasicSetter<decltype(mCurrentWorkStatus)::element_type>)
                .build());
    }

private:
    // setters
    static C2R BlockSizeSetter(bool mayBlock,
            C2InterfaceHelper::C2P<C2StreamBlockSizeInfo::output> &me) {
        (void)mayBlock;
        uint32_t width_ = c2_min(me.v.width, 8192u);
        uint32_t height_ = c2_min(me.v.height, 8192u);
        if (width_ % 2 != 0) width_++;
        if (height_ % 2 != 0) height_++;
        me.set().width = width_;
        me.set().height = height_;
        return C2R::Ok();
    }
    static C2R BlockCountSetter(bool mayBlock,
            C2InterfaceHelper::C2P<C2StreamBlockCountInfo::output> &me) {
        (void)mayBlock;
        me.set().value = c2_min(me.v.value, kDefaultImageBufferCount);
        return C2R::Ok();
    }

private:
    // buffer configuraration
    std::shared_ptr<C2StreamBlockSizeInfo::output> mBlockSize;
    std::shared_ptr<C2StreamBlockCountInfo::output> mBlockCount;
    std::shared_ptr<C2StreamPixelFormatInfo::output> mPixelFormat;
    std::shared_ptr<C2StreamUsageTuning::output> mUsage;

    // input surface source configuration
    std::shared_ptr<C2PortMinFrameRateTuning::output> mMinFps;
    std::shared_ptr<C2PortMaxFrameRateTuning::output> mMaxFps;
    std::shared_ptr<C2PortCaptureFrameRateTuning::output> mCaptureFps;
    std::shared_ptr<C2StreamFrameRateInfo::output> mCodedFps;
    std::shared_ptr<C2ComponentTimeOffsetTuning> mTimeOffset; // unsigned, but
                                                              // signed
    std::shared_ptr<C2PortStartTimestampTuning::output> mStarted;
    std::shared_ptr<C2PortStopTimestampTuning::output> mStopped;
    std::shared_ptr<C2PortSuspendTimestampTuning::output> mSuspended;
    std::shared_ptr<C2PortResumeTimestampTuning::output> mResumed;
    std::shared_ptr<C2PortTimestampGapTuning::output> mGap;
    std::shared_ptr<C2PortStopTimeOffset::output> mStopTimeOffset; // query only

    // current work status configuration
    // informations about recent input buffers being done and emptied.
    // TODO: remove this and move this to onWorkDone()
    std::shared_ptr<C2CurrentWorkTuning> mCurrentWorkStatus;
};

class InputSurface::ConfigurableIntf : public ConfigurableC2Intf {
public:

    ConfigurableIntf(
            const std::shared_ptr<InputSurface::Interface> &intf)
        : ConfigurableC2Intf("input-surface", 0),
          mIntf(intf) {
    }

    virtual ~ConfigurableIntf() override = default;

    virtual c2_status_t query(
            const std::vector<C2Param::Index> &indices,
            c2_blocking_t mayBlock,
            std::vector<std::unique_ptr<C2Param>>* const params
            ) const override {
        return mIntf->query({}, indices, mayBlock, params);
    }

    virtual c2_status_t config(
            const std::vector<C2Param*> &params,
            c2_blocking_t mayBlock,
            std::vector<std::unique_ptr<C2SettingResult>>* const failures
            ) override {
        c2_status_t err = mIntf->config(params, mayBlock, failures);
        return err;
    }

    virtual c2_status_t querySupportedParams(
            std::vector<std::shared_ptr<C2ParamDescriptor>>* const params
            ) const override {
        return mIntf->querySupportedParams(params);
    }

    virtual c2_status_t querySupportedValues(
            std::vector<C2FieldSupportedValuesQuery>& fields,
            c2_blocking_t mayBlock) const override {
        return mIntf->querySupportedValues(fields, mayBlock);
    }
private:
    const std::shared_ptr<InputSurface::Interface> mIntf;
};

struct InputSurface::DeathContext {
    // TODO;
};

void InputSurface::OnBinderDied(void *cookie) {
    (void) cookie;
}

void InputSurface::OnBinderUnlinked(void *cookie) {
    (void) cookie;
}

InputSurface::InputSurface() : mDeathContext(nullptr) {
    mIntf = std::make_shared<Interface>(std::make_shared<C2ReflectorHelper>());
    mInit = C2_OK;
}

InputSurface::~InputSurface() {
}

::ndk::ScopedAStatus InputSurface::getSurface(::aidl::android::view::Surface* surface) {
    (void) surface;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus InputSurface::getConfigurable(
        std::shared_ptr<IConfigurable>* configurable) {
    *configurable = mConfigurable;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus InputSurface::connect(
        const std::shared_ptr<IInputSink>& sink,
        std::shared_ptr<IInputSurfaceConnection>* connection) {
    (void) sink;
    (void) connection;
    return ::ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::media::c2::utils
