/*
 * Copyright (C) 2019 The Android Open Source Project
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

#define LOG_NDEBUG 0
#define LOG_TAG "C2SoftApvEnc"
#include <log/log.h>

#include <media/hardware/VideoAPI.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/foundation/AUtils.h>

#include <C2Debug.h>
#include <Codec2CommonUtils.h>
#include <Codec2Mapper.h>
#include <C2PlatformSupport.h>
#include <Codec2BufferUtils.h>
#include <SimpleC2Interface.h>
#include <util/C2InterfaceHelper.h>

#include "C2SoftApvEnc.h"

namespace android {

namespace {

const char APV_ENCODER_VERSION[] = "1.0.6";
//const char *MEDIA_MIMETYPE_VIDEO_APV = "video/apv";
constexpr char COMPONENT_NAME[] = "c2.android.apv.encoder";
constexpr uint32_t kMinOutBufferSize = 524288;
constexpr uint32_t kMaxBitstreamBufSize = 16 * 1024 * 1024;
constexpr int32_t kApvQpMin = 0;
constexpr int32_t kApvQpMax = 51;
constexpr int32_t kApvDefaultQP = 32;

#define PROFILE_APV_DEFAULT  0
#define LEVEL_APV_DEFAULT    0
#define MAX_NUM_FRMS (1)  // supports only 1-frame input

} // namepsace


class C2SoftApvEnc::IntfImpl : public SimpleInterface<void>::BaseParams {
  public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper)
        : SimpleInterface<void>::BaseParams(
                helper,
                COMPONENT_NAME,
                C2Component::KIND_ENCODER,
                C2Component::DOMAIN_VIDEO,
                "video/apv") {
//                MEDIA_MIMETYPE_VIDEO_APV) {
        noPrivateBuffers();
        noInputReferences();
        noOutputReferences();
        noTimeStretch();
        setDerivedInstance(this);

        addParameter(
            DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
                .withConstValue(new C2ComponentAttributesSetting(
                    C2Component::ATTRIB_IS_TEMPORAL))
                .build());

        addParameter(
            DefineParam(mUsage, C2_PARAMKEY_INPUT_STREAM_USAGE)
                .withConstValue(new C2StreamUsageTuning::input(
                        0u, (uint64_t)C2MemoryUsage::CPU_READ))
                .build());

        // matches size limits in codec library
        addParameter(
            DefineParam(mSize, C2_PARAMKEY_PICTURE_SIZE)
                .withDefault(new C2StreamPictureSizeInfo::input(0u, 320, 240))
                .withFields({
                    C2F(mSize, width).inRange(2, 1920, 2),
                    C2F(mSize, height).inRange(2, 1088, 2),
                })
                .withSetter(SizeSetter)
                .build());

        // matches limits in codec library
        addParameter(
            DefineParam(mBitrateMode, C2_PARAMKEY_BITRATE_MODE)
                .withDefault(new C2StreamBitrateModeTuning::output(
                        0u, C2Config::BITRATE_VARIABLE))
                .withFields({
                    C2F(mBitrateMode, value).oneOf({
                        C2Config::BITRATE_CONST,
                        C2Config::BITRATE_VARIABLE,
                        C2Config::BITRATE_IGNORE})
                })
                .withSetter(
                    Setter<decltype(*mBitrateMode)>::StrictValueWithNoDeps)
                .build());

        addParameter(
            DefineParam(mBitrate, C2_PARAMKEY_BITRATE)
                .withDefault(new C2StreamBitrateInfo::output(0u, 512000))
                .withFields({C2F(mBitrate, value).inRange(512000, 240000000)})
                .withSetter(BitrateSetter)
                .build());

        addParameter(
	        DefineParam(mFrameRate, C2_PARAMKEY_FRAME_RATE)
                .withDefault(new C2StreamFrameRateInfo::output(0u, 15.))
                .withFields({C2F(mFrameRate, value).greaterThan(0.)})
                .withSetter(Setter<decltype(*mFrameRate)>::StrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mQuality, C2_PARAMKEY_QUALITY)
                .withDefault(new C2StreamQualityTuning::output(0u, 40))
                .withFields({C2F(mQuality, value).inRange(0, 100)})
                .withSetter(Setter<decltype(*mQuality)>::NonStrictValueWithNoDeps)
                .build());

        addParameter(
                DefineParam(mComplexity, C2_PARAMKEY_COMPLEXITY)
                .withDefault(new C2StreamComplexityTuning::output(0u, 1))
                .withFields({C2F(mComplexity, value).inRange(0, 1)})
                .withSetter(Setter<decltype(*mComplexity)>::StrictValueWithNoDeps)
                .build());
/*
        addParameter(
                DefineParam(mBandIdc, C2_PARAMKEY_APV_BAND_IDC)
                .withDefault(new C2StreamApvBandIdc::output(0u, 1))
                .withFields({C2F(mBandIdc, value).inRange(0, 3)})
                .withSetter(Setter<decltype(*mBandIdc)>::StrictValueWithNoDeps)
                .build());
*/
        addParameter(
            DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                .withDefault(new C2StreamProfileLevelInfo::output(
                    0u, C2Config::PROFILE_APV_422_10, LEVEL_APV_1_BAND_0))
                .withFields({
                    C2F(mProfileLevel, profile)
                        .oneOf({C2Config::PROFILE_APV_422_10}),
                    C2F(mProfileLevel, level)
                        .oneOf({
                            C2Config::LEVEL_APV_1_BAND_0, C2Config::LEVEL_APV_1_1_BAND_0,
                            C2Config::LEVEL_APV_2_BAND_0, C2Config::LEVEL_APV_2_1_BAND_0,
                            C2Config::LEVEL_APV_3_BAND_0, C2Config::LEVEL_APV_3_1_BAND_0,
                            C2Config::LEVEL_APV_4_BAND_0, C2Config::LEVEL_APV_4_1_BAND_0,
                            C2Config::LEVEL_APV_5_BAND_0, C2Config::LEVEL_APV_5_1_BAND_0,
                            C2Config::LEVEL_APV_6_BAND_0, C2Config::LEVEL_APV_6_1_BAND_0,
                            C2Config::LEVEL_APV_7_BAND_0, C2Config::LEVEL_APV_7_1_BAND_0,
                        }),
                })
                .withSetter(ProfileLevelSetter, mSize, mFrameRate, mBitrate)
                .build());

        addParameter(
            DefineParam(mColorAspects, C2_PARAMKEY_COLOR_ASPECTS)
                .withDefault(new C2StreamColorAspectsInfo::input(
                        0u, C2Color::RANGE_UNSPECIFIED, C2Color::PRIMARIES_UNSPECIFIED,
                        C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
                .withFields({
                    C2F(mColorAspects, range).inRange(
                                C2Color::RANGE_UNSPECIFIED,     C2Color::RANGE_OTHER),
                    C2F(mColorAspects, primaries).inRange(
                                C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                    C2F(mColorAspects, transfer).inRange(
                                C2Color::TRANSFER_UNSPECIFIED,  C2Color::TRANSFER_OTHER),
                    C2F(mColorAspects, matrix).inRange(
                                C2Color::MATRIX_UNSPECIFIED,    C2Color::MATRIX_OTHER)
                })
                .withSetter(ColorAspectsSetter)
                .build());

        addParameter(
            DefineParam(mCodedColorAspects, C2_PARAMKEY_VUI_COLOR_ASPECTS)
                .withDefault(new C2StreamColorAspectsInfo::output(
                        0u, C2Color::RANGE_LIMITED, C2Color::PRIMARIES_UNSPECIFIED,
                        C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
                .withFields({
                    C2F(mCodedColorAspects, range).inRange(
                                C2Color::RANGE_UNSPECIFIED,     C2Color::RANGE_OTHER),
                    C2F(mCodedColorAspects, primaries).inRange(
                                C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                    C2F(mCodedColorAspects, transfer).inRange(
                                C2Color::TRANSFER_UNSPECIFIED,  C2Color::TRANSFER_OTHER),
                    C2F(mCodedColorAspects, matrix).inRange(
                                C2Color::MATRIX_UNSPECIFIED,    C2Color::MATRIX_OTHER)
                })
                .withSetter(CodedColorAspectsSetter, mColorAspects)
                .build());
            std::vector<uint32_t> pixelFormats = {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
                                        };
            if (isHalPixelFormatSupported((AHardwareBuffer_Format)HAL_PIXEL_FORMAT_YCBCR_P010)) {
                pixelFormats.push_back(HAL_PIXEL_FORMAT_YCBCR_P010);
            }
            addParameter(DefineParam(mPixelFormat, C2_PARAMKEY_PIXEL_FORMAT)
                .withDefault(new C2StreamPixelFormatInfo::input(
                        0u, HAL_PIXEL_FORMAT_YCBCR_P010))
                .withFields({C2F(mPixelFormat, value).oneOf({pixelFormats})})
                .withSetter((Setter<decltype(*mPixelFormat)>::StrictValueWithNoDeps))
                .build());

    }

    static C2R ComplexitySetter(bool mayBlock,
                                C2P<C2StreamComplexityTuning::output>& me) {
        (void)mayBlock;
        C2R res = C2R::Ok();
        if (me.v.value > 1) {
            me.set().value = 0;
        }
        return res;
    }

    static C2R BitrateSetter(bool mayBlock,
                             C2P<C2StreamBitrateInfo::output>& me) {
        (void)mayBlock;
        C2R res = C2R::Ok();
        if (me.v.value < 1000000) {
            me.set().value = 1000000;
        }
        return res;
    }

    static C2R SizeSetter(bool mayBlock,
                          const C2P<C2StreamPictureSizeInfo::input>& oldMe,
                          C2P<C2StreamPictureSizeInfo::input>& me) {
        (void)mayBlock;
        C2R res = C2R::Ok();
        if (!me.F(me.v.width).supportsAtAll(me.v.width)) {
            res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.width)));
            me.set().width = oldMe.v.width;
        }
        if (!me.F(me.v.height).supportsAtAll(me.v.height)) {
            res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.height)));
            me.set().height = oldMe.v.height;
        }
        return res;
    }

    static C2R ProfileLevelSetter(
            bool mayBlock,
            C2P<C2StreamProfileLevelInfo::output> &me,
            const C2P<C2StreamPictureSizeInfo::input> &size,
            const C2P<C2StreamFrameRateInfo::output> &frameRate,
            const C2P<C2StreamBitrateInfo::output> &bitrate) {
        (void)mayBlock;
        if (!me.F(me.v.profile).supportsAtAll(me.v.profile)) {
            me.set().profile = C2Config::PROFILE_APV_422_10;
        }
        if (!me.F(me.v.level).supportsAtAll(me.v.level)) {
            me.set().level = LEVEL_APV_1_BAND_0;
        }
        return C2R::Ok();
    }

    static C2R ColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsInfo::input> &me) {
        (void)mayBlock;
        if (me.v.range > C2Color::RANGE_OTHER) {
                me.set().range = C2Color::RANGE_OTHER;
        }
        if (me.v.primaries > C2Color::PRIMARIES_OTHER) {
                me.set().primaries = C2Color::PRIMARIES_OTHER;
        }
        if (me.v.transfer > C2Color::TRANSFER_OTHER) {
                me.set().transfer = C2Color::TRANSFER_OTHER;
        }
        if (me.v.matrix > C2Color::MATRIX_OTHER) {
                me.set().matrix = C2Color::MATRIX_OTHER;
        }
        return C2R::Ok();
    }

    static C2R CodedColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsInfo::output> &me,
                                       const C2P<C2StreamColorAspectsInfo::input> &coded) {
        (void)mayBlock;
        me.set().range = coded.v.range;
        me.set().primaries = coded.v.primaries;
        me.set().transfer = coded.v.transfer;
        me.set().matrix = coded.v.matrix;
        return C2R::Ok();
    }

    uint32_t getProfile_l() const {
        int32_t profile = PROFILE_UNUSED;

        switch (mProfileLevel->profile) {
        case C2Config::PROFILE_APV_422_10:
            profile = 33;
            break;
        case C2Config::PROFILE_APV_422_12:
            profile = 44;
            break;
        case C2Config::PROFILE_APV_444_10:
            profile = 55;
            break;
        case C2Config::PROFILE_APV_444_12:
            profile = 66;
            break;
        case C2Config::PROFILE_APV_4444_10:
            profile = 77;
            break;
        case C2Config::PROFILE_APV_4444_12:
            profile = 88;
            break;
        case C2Config::PROFILE_APV_400_10:
            profile = 99;
            break;
        default:
            ALOGD("Unrecognized profile: %x", mProfileLevel->profile);
        }
        return profile;
    }

    uint32_t getLevel_l() const {
        int32_t level = PROFILE_UNUSED;

        switch (mProfileLevel->level) {
        case C2Config::LEVEL_APV_1_BAND_0:
            level = 10;
            break;
        case C2Config::LEVEL_APV_1_1_BAND_0:
            level = 11;
            break;
        case C2Config::LEVEL_APV_2_BAND_0:
            level = 20;
            break;
        case C2Config::LEVEL_APV_2_1_BAND_0:
            level = 21;
            break;
        case C2Config::LEVEL_APV_3_BAND_0:
            level = 30;
            break;
        case C2Config::LEVEL_APV_3_1_BAND_0:
            level = 31;
            break;
        case C2Config::LEVEL_APV_4_BAND_0:
            level = 40;
            break;
        case C2Config::LEVEL_APV_4_1_BAND_0:
            level = 41;
            break;
        case C2Config::LEVEL_APV_5_BAND_0:
            level = 50;
            break;
        case C2Config::LEVEL_APV_5_1_BAND_0:
            level = 51;
            break;
        case C2Config::LEVEL_APV_6_BAND_0:
            level = 60;
            break;
        case C2Config::LEVEL_APV_6_1_BAND_0:
            level = 61;
            break;
        case C2Config::LEVEL_APV_7_BAND_0:
            level = 70;
            break;
        case C2Config::LEVEL_APV_7_1_BAND_0:
            level = 71;
            break;
        default:
            ALOGD("Unrecognized level: %x", mProfileLevel->level);
        }
        return level * 3;
    }

    int32_t getBitrateMode_l() const {
        int32_t bitrateMode = C2Config::BITRATE_CONST;

        switch (mBitrateMode->value) {
        case C2Config::BITRATE_CONST:
            bitrateMode = OAPV_RC_CQP;
            break;
        case C2Config::BITRATE_VARIABLE:
            bitrateMode = OAPV_RC_ABR;
            break;
        case C2Config::BITRATE_IGNORE:
            bitrateMode = 0;
            break;
        default:
            ALOGE("Unrecognized bitrate mode: %x", mBitrateMode->value);
        }
        return bitrateMode;
    }

    std::shared_ptr<C2StreamPictureSizeInfo::input> getSize_l() const {
        return mSize;
    }
    std::shared_ptr<C2StreamFrameRateInfo::output> getFrameRate_l() const {
        return mFrameRate;
    }
    std::shared_ptr<C2StreamBitrateInfo::output> getBitrate_l() const {
        return mBitrate;
    }
    std::shared_ptr<C2StreamComplexityTuning::output> getComplexity_l() const {
        return mComplexity;
    }
    std::shared_ptr<C2StreamQualityTuning::output> getQuality_l() const {
        return mQuality;
    }
    std::shared_ptr<C2StreamColorAspectsInfo::input> getColorAspects_l() const {
        return mColorAspects;
    }
    std::shared_ptr<C2StreamColorAspectsInfo::output> getCodedColorAspects_l() const {
        return mCodedColorAspects;
    }
    std::shared_ptr<C2StreamPictureQuantizationTuning::output> getPictureQuantization_l() const {
        return mPictureQuantization;
    }
    std::shared_ptr<C2StreamProfileLevelInfo::output> getProfileLevel_l() const {
        return mProfileLevel;
    }
    std::shared_ptr<C2StreamPixelFormatInfo::input> getPixelFormat_l() const {
        return mPixelFormat;
    }

/*    
    std::shared_ptr<C2StreamApvBandIdc::output> getBandIdc_l() const {
        return mBandIdc;
    }
*/
private:
    std::shared_ptr<C2StreamProfileLevelInfo::output> mProfileLevel;
    std::shared_ptr<C2StreamUsageTuning::input> mUsage;
    std::shared_ptr<C2StreamPictureSizeInfo::input> mSize;
    std::shared_ptr<C2StreamFrameRateInfo::output> mFrameRate;
    std::shared_ptr<C2StreamBitrateInfo::output> mBitrate;
    std::shared_ptr<C2StreamBitrateModeTuning::output> mBitrateMode;
    std::shared_ptr<C2StreamComplexityTuning::output> mComplexity;
    std::shared_ptr<C2StreamQualityTuning::output> mQuality;
    std::shared_ptr<C2StreamColorAspectsInfo::input> mColorAspects;
    std::shared_ptr<C2StreamColorAspectsInfo::output> mCodedColorAspects;
    std::shared_ptr<C2StreamPictureQuantizationTuning::output> mPictureQuantization;
    std::shared_ptr<C2StreamColorInfo::input> mColorFormat;
    std::shared_ptr<C2StreamPixelFormatInfo::input> mPixelFormat;
    //std::shared_ptr<C2StreamApvBandIdc::output> mBandIdc;
};

C2SoftApvEnc::C2SoftApvEnc(const char* name, c2_node_id_t id,
                             const std::shared_ptr<IntfImpl>& intfImpl)
    : SimpleC2Component(
          std::make_shared<SimpleInterface<IntfImpl>>(name, id, intfImpl)),
    mIntf(intfImpl),
    mColorFormat(OAPV_CF_PLANAR2),
    mStarted(false),
    mSignalledEos(false),
    mSignalledError(false),
    mOutBlock(nullptr) {
    ALOGI("APV Encoder version: %s", APV_ENCODER_VERSION);
    reset();
}

C2SoftApvEnc::~C2SoftApvEnc() {
    onRelease();
}

c2_status_t C2SoftApvEnc::onInit() {
    return C2_OK;
}

c2_status_t C2SoftApvEnc::onStop() {
    return C2_OK;
}

void C2SoftApvEnc::onReset() {
    releaseEncoder();
    reset();
}

void C2SoftApvEnc::onRelease() {
    releaseEncoder();
}

c2_status_t C2SoftApvEnc::onFlush_sm() {
    return C2_OK;
}

static void fillEmptyWork(const std::unique_ptr<C2Work>& work) {
    uint32_t flags = 0;
    if (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) {
        flags |= C2FrameData::FLAG_END_OF_STREAM;
        ALOGV("Signalling EOS");
    }
    work->worklets.front()->output.flags = (C2FrameData::flags_t)flags;
    work->worklets.front()->output.buffers.clear();
    work->worklets.front()->output.ordinal = work->input.ordinal;
    work->workletsProcessed = 1u;
}

int32_t C2SoftApvEnc::getQpFromQuality(int32_t quality) {
    int32_t qp = ((kApvQpMin - kApvQpMax) * quality / 100) + kApvQpMax;
    qp = std::min(qp, (int)kApvQpMax);
    qp = std::max(qp, (int)kApvQpMin);
    return qp;
}

c2_status_t C2SoftApvEnc::reset() {
    ALOGV("reset");
    mInitEncoder = false;
    mStarted = false;
    mSignalledEos = false;
    mSignalledError = false;
    mBitDepth = 10;
    mMaxFrames = MAX_NUM_FRMS;
    mReceivedFrames = 0;
#if 1
    mColorFormat = OAPV_CF_PLANAR2;
#else
    mColorFormat = OAPV_CF_YCBCR422;
#endif
    return C2_OK;
}

c2_status_t C2SoftApvEnc::releaseEncoder() {
    for (int32_t i = 0; i < MAX_NUM_FRMS; i++) {
        if (InputFrames.frm[i].imgb != nullptr) {
            imgb_release(InputFrames.frm[i].imgb);
        }
    }

    if (mBitstreamBuf) {
        std::free(mBitstreamBuf);
        mBitstreamBuf = nullptr;
    }
    return C2_OK;
}

c2_status_t C2SoftApvEnc::drain(uint32_t drainMode,
                                 const std::shared_ptr<C2BlockPool>& pool) {
    return drainInternal(drainMode, pool, nullptr);
}

void C2SoftApvEnc::showEncoderParams(oapve_cdesc_t* cdsc) {
    std::string title = "APV encoder params:";
    ALOGD("%s width = %d, height = %d", title.c_str(), cdsc->param[0].w, cdsc->param[0].h);
    ALOGD("%s FrameRate = %f", title.c_str(), (double)cdsc->param[0].fps_num/cdsc->param[0].fps_den);
    ALOGD("%s BitRate = %d Kbps", title.c_str(), cdsc->param[0].bitrate);
    ALOGD("%s QP = %d", title.c_str(), cdsc->param[0].qp);
    ALOGD("%s profile_idc = %d, level_idc = %d, band_idc = %d", title.c_str(), cdsc->param[0].profile_idc, cdsc->param[0].level_idc, cdsc->param[0].band_idc);
    ALOGD("%s Bitrate Mode: %d", title.c_str(), cdsc->param[0].rc_type);
    ALOGD("%s mColorAspects primaries: %d, transfer: %d, matrix: %d, range: %d", 
        title.c_str(), mColorAspects->primaries, mColorAspects->transfer, mColorAspects->matrix, mColorAspects->range);
    ALOGD("%s mCodedColorAspects primaries: %d, transfer: %d, matrix: %d, range: %d", 
        title.c_str(), mCodedColorAspects->primaries, mCodedColorAspects->transfer, mCodedColorAspects->matrix, mCodedColorAspects->range);
    ALOGD("%s Input color format: %s", title.c_str(), mColorFormat == OAPV_CF_YCBCR422 ? "YUV422P10LE" : "P210");
    ALOGD("%s max_num_frms: %d", title.c_str(), cdsc->max_num_frms);
}

c2_status_t C2SoftApvEnc::initEncoder() {
    if (mInitEncoder) {
        return C2_OK;
    }
    ALOGV("initEncoder");

    mSize = mIntf->getSize_l();
    mFrameRate = mIntf->getFrameRate_l();
    mBitrate = mIntf->getBitrate_l();
    mQuality = mIntf->getQuality_l();
    mComplexity = mIntf->getComplexity_l();
    mColorAspects = mIntf->getColorAspects_l();
    mCodedColorAspects = mIntf->getCodedColorAspects_l();
    mProfileLevel = mIntf->getProfileLevel_l();
    mPixelFormat = mIntf->getPixelFormat_l();
    //mBandIdc = mIntf->getBandIdc_l();

    mCodecDesc = std::make_unique<oapve_cdesc_t>();
    if (mCodecDesc == nullptr) {
        ALOGE("Allocate ctx failed");
        return C2_NO_INIT;
    }
    mCodecDesc->max_bs_buf_size = kMaxBitstreamBufSize;
    mCodecDesc->max_num_frms = MAX_NUM_FRMS;
    mCodecDesc->threads = 4;

    int32_t ret = C2_OK;
    /* set params */
    for (int32_t i = 0; i < mMaxFrames; i++) {
        oapve_param_t *param = &mCodecDesc->param[i];
        ret = oapve_param_default(param);
        if (OAPV_FAILED(ret)) {
            ALOGE("cannot set default parameter");
            return C2_NO_INIT;
        }
        setParams(*param);
    }

    showEncoderParams(mCodecDesc.get());

    /* create encoder */
    mEncoderId = oapve_create(mCodecDesc.get(), NULL);
    if (mEncoderId == NULL) {
        ALOGE("cannot create APV encoder");
        return C2_CORRUPTED;
    }

    /* create metadata */
    mMetaId = oapvm_create(&ret);
    if (mMetaId == NULL) {
        ALOGE("cannot create APV encoder");
        return C2_NO_MEMORY;
    }

    /* create image buffers */
    for (int32_t i = 0; i < mMaxFrames; i++) {
        if (mBitDepth == 10) {
            InputFrames.frm[i].imgb = imgb_create(mCodecDesc->param[0].w, mCodecDesc->param[0].h, OAPV_CS_SET(mColorFormat, mBitDepth, 0));
            ReconFrames.frm[i].imgb = nullptr;
        }
        else {
            InputFrames.frm[i].imgb = imgb_create(mCodecDesc->param[0].w, mCodecDesc->param[0].h, OAPV_CS_SET(mColorFormat, 10, 0));
            ReconFrames.frm[i].imgb = nullptr;
        }
    }

    /* allocate bitstream buffer */
    mBitstreamBuf = new unsigned char[kMaxBitstreamBufSize];
    if(mBitstreamBuf == nullptr) {
        ALOGE("cannot allocate bitstream buffer, size= %d", kMaxBitstreamBufSize);
        return C2_NO_MEMORY;
    }

    /* Calculate SDR to HDR mapping values */
    mSdrToHdrMapping.clear();
    for (int32_t i = 0; i < 256; i++) {
        mSdrToHdrMapping.push_back((uint16_t)(i * 1023 / 255 + 0.5));
    }

    mStarted = true;
    mInitEncoder = true;
    return C2_OK;
}

void C2SoftApvEnc::setParams(oapve_param_t &param) {
    param.w = mSize->width;
    param.h = mSize->height;
    param.fps_num = (int)(mFrameRate->value * 100);
    param.fps_den = 100;
    param.bitrate = mBitrate->value / 1000;
    param.rc_type = mIntf->getBitrateMode_l();

    int ApvQP = kApvDefaultQP;
    if (param.rc_type == OAPV_RC_CQP) {
        ApvQP = getQpFromQuality(mQuality->value);
        ALOGI("Bitrate mode is CQ, so QP value is derived from Quality. Quality is %d, QP is %d", mQuality->value, ApvQP);
    }
    param.qp = ApvQP;

    param.profile_idc = mIntf->getProfile_l();
    param.level_idc = mIntf->getLevel_l();
    param.band_idc = 0;//mBandIdc->value;
}

c2_status_t C2SoftApvEnc::setEncodeArgs(
        oapv_frms_t* InputFrames, 
        const C2GraphicView *const input,
        uint64_t workIndex) {
    if (input->width() < mSize->width ||
        input->height() < mSize->height) {
        /* Expect width height to be configured */
        ALOGW("unexpected Capacity Aspect %d(%d) x %d(%d)", input->width(),
              mSize->width, input->height(), mSize->height);
        return C2_BAD_VALUE;
    }
    const C2PlanarLayout &layout = input->layout();
    uint8_t *yPlane = const_cast<uint8_t *>(input->data()[C2PlanarLayout::PLANE_Y]);
    uint8_t *uPlane = const_cast<uint8_t *>(input->data()[C2PlanarLayout::PLANE_U]);
    uint8_t *vPlane = const_cast<uint8_t *>(input->data()[C2PlanarLayout::PLANE_V]);
    int32_t yStride = layout.planes[C2PlanarLayout::PLANE_Y].rowInc;
    int32_t uStride = layout.planes[C2PlanarLayout::PLANE_U].rowInc;
    int32_t vStride = layout.planes[C2PlanarLayout::PLANE_V].rowInc;

    uint32_t width = mSize->width;
    uint32_t height = mSize->height;

   /* width and height must be even */
    if (width & 1u || height & 1u) {
        ALOGW("height(%u) and width(%u) must both be even", height, width);
        return C2_BAD_VALUE;
    }

    /* Set num frames */
    InputFrames->num_frms = MAX_NUM_FRMS;
    InputFrames->frm[mReceivedFrames].group_id = 1;
    InputFrames->frm[mReceivedFrames].pbu_type = OAPV_PBU_TYPE_PRIMARY_FRAME;
    
    switch (layout.type) {
        case C2PlanarLayout::TYPE_RGB:
            [[fallthrough]];
        case C2PlanarLayout::TYPE_RGBA: {
            ALOGE("Not supported RGB color format");
            return C2_BAD_VALUE;
        }
        case C2PlanarLayout::TYPE_YUV: {
            if (IsP010(*input)) {
                ALOGV("Input layout P010");
                if (mColorFormat == OAPV_CF_YCBCR422) {
                    ColorConvertP010ToYUV422P10le(input, InputFrames->frm[0].imgb);
                }
                else if (mColorFormat == OAPV_CF_PLANAR2) {
                    ColorConvertP010ToP210(input, InputFrames->frm[0].imgb);
                }
                else {
                    ALOGE("Not supported color format. %d", mColorFormat);
                    return C2_BAD_VALUE;
                }
            }
            else if (IsNV12(*input)) {
                ALOGV("Input layout NV12");
                ColorConvertNv12ToP210(input, InputFrames->frm[0].imgb);
            }
            else if (IsNV21(*input)) {
                ALOGV("Input layout NV21");
                ColorConvertNv12ToP210(input, InputFrames->frm[0].imgb);
            }
            else if (IsYUV420(*input)) {
                return C2_BAD_VALUE;
            }
            else if (IsI420(*input)) {
                return C2_BAD_VALUE;
            }
            else {
                ALOGE("Not supported color format. %d", mColorFormat);
                return C2_BAD_VALUE;
            }
            break;
        }
        case C2PlanarLayout::TYPE_YUVA:
            ALOGE("YUVA plane type is not supported");
            return C2_BAD_VALUE;

        default:
            ALOGE("Unrecognized plane type: %d", layout.type);
            return C2_BAD_VALUE;
    }
    
    return C2_OK;
}

void C2SoftApvEnc::ColorConvertNv12ToP210(
        const C2GraphicView *const input, oapv_imgb_t* imgb) {
    auto width = input->width();
    auto height = input->height();

    auto *yPlane = (uint8_t*)input->data()[0];
    auto *uvPlane = (uint8_t*)input->data()[1];

    auto *dst = (uint16_t*)imgb->a[0];
    int32_t lumaOffset = 0;
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            lumaOffset = y * width + x;
            dst[lumaOffset] = (mSdrToHdrMapping[yPlane[lumaOffset]] << 6) |
                                ((mSdrToHdrMapping[yPlane[lumaOffset]] & 0x300) >> 3);
        }
    }

    auto *dst_uv = (uint16_t*)imgb->a[1];
    uint32_t uvDstStride = width;
    int32_t srcOffset = 0;
    int32_t dstOffset1 = 0, dstOffset2 = 0;
    int32_t tmp1 = 0, tmp2 = 0;
    for (int32_t y = 0; y < height / 2; ++y) {
        for (int32_t x = 0; x < width; x += 2) {
            srcOffset = y * width + x;
            dstOffset1 = (y * 2) * width + x;
            dstOffset2 = ((y * 2) + 1) * width + x;

            tmp1 = (mSdrToHdrMapping[uvPlane[srcOffset]] << 6) |
                    ((mSdrToHdrMapping[uvPlane[srcOffset]] & 0x300) >> 3);
            tmp2 = (mSdrToHdrMapping[uvPlane[srcOffset + 1]] << 6) |
                    ((mSdrToHdrMapping[uvPlane[srcOffset + 1]] & 0x300) >> 3);
            dst_uv[dstOffset1] = (uint16_t)tmp1;
            dst_uv[dstOffset1 + 1] = (uint16_t)tmp2;
            dst_uv[dstOffset2] = (uint16_t)tmp1;
            dst_uv[dstOffset2 + 1] = (uint16_t)tmp2;
        }
    }
}

void C2SoftApvEnc::ColorConvertP010ToP210(
        const C2GraphicView *const input, oapv_imgb_t* imgb) {
    auto width = input->width();
    auto height = input->height();

    auto *yPlane = (uint8_t*)input->data()[0];
    auto *uvPlane = (uint8_t*)input->data()[1];
    uint32_t uvStride = width * 2;

    auto *src = yPlane;
    auto *dst = (uint8_t*)imgb->a[0];
    std::memcpy(dst, src, width * height * 2);

    auto *dst_uv = (uint8_t*)imgb->a[1];
    int32_t offset1 = 0, offset2 = 0;
    for (int32_t y = 0; y < height / 2; ++y) {
        offset1 = (y * 2) * uvStride;
        offset2 = (y * 2 + 1) * uvStride;
        src = uvPlane + (y * uvStride);

        std::memcpy(dst_uv + offset1, src, uvStride);
        std::memcpy(dst_uv + offset2, src, uvStride);
    }
}

void C2SoftApvEnc::ColorConvertP010ToYUV422P10le(
        const C2GraphicView *const input, oapv_imgb_t* imgb) {
    uint32_t width = input->width();
    uint32_t height = input->height();

    uint8_t *yPlane = (uint8_t*)input->data()[0];
    auto *uvPlane = (uint8_t*)input->data()[1];
    uint32_t stride[3];
    stride[0] = width * 2;
    stride[1] = stride[2] = width;

    uint8_t *dst, *src;
    uint16_t tmp;
    for (int32_t y = 0; y < height; ++y) {
        src = yPlane + y * stride[0];
        dst = (uint8_t*)imgb->a[0] + y * stride[0];
        for (int32_t x = 0; x < stride[0]; x += 2) {
            tmp = (src[x + 1] << 2) | (src[x] >> 6);
            dst[x] = tmp & 0xFF;
            dst[x + 1] = tmp >> 8;
        }
    }

    uint8_t *dst_u, *dst_v;
    for (int32_t y = 0; y < height / 2; ++y) {
        src = uvPlane + y * stride[1] * 2;
        dst_u = (uint8_t*)imgb->a[1] + (y * 2) * stride[1];
        dst_v = (uint8_t*)imgb->a[2] + (y * 2) * stride[2];
        for (int32_t x = 0; x < stride[1] * 2; x += 4) {
            tmp = (src[x + 1] << 2) | (src[x] >> 6); // cb
            dst_u[x / 2] = tmp & 0xFF;
            dst_u[x / 2 + 1] = tmp >> 8;
            dst_u[x / 2 + stride[1]] = dst_u[x / 2];
            dst_u[x / 2 + stride[1] + 1] = dst_u[x / 2 + 1];

            tmp = (src[x + 3] << 2) | (src[x + 2] >> 6); // cr
            dst_v[x / 2] = tmp & 0xFF;
            dst_v[x / 2 + 1] = tmp >> 8;
            dst_v[x / 2 + stride[2]] = dst_v[x / 2];
            dst_v[x / 2 + stride[2] + 1] = dst_v[x / 2 + 1];
        }
    }
}

void C2SoftApvEnc::finishWork(uint64_t workIndex, const std::unique_ptr<C2Work> &work,
                              const std::shared_ptr<C2BlockPool>& pool,
                              oapv_bitb_t *mBitb, oapve_stat_t* stat) {
    std::shared_ptr<C2LinearBlock> block;
    C2MemoryUsage usage = {C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE};
    c2_status_t status =
        pool->fetchLinearBlock(stat->write, usage, &block);
    if (C2_OK != status) {
        ALOGE("fetchLinearBlock for Output failed with status 0x%x", status);
        mSignalledError = true;
        work->result = status;
        work->workletsProcessed = 1u;
        return;
    }

    C2WriteView wView = block->map().get();
    if (C2_OK != wView.error()) {
        ALOGE("write view map failed with status 0x%x", wView.error());
        mSignalledError = true;
        work->result = wView.error();
        work->workletsProcessed = 1u;
        return;
    }
    memcpy(wView.data(), mBitb->addr, stat->write);

    std::shared_ptr<C2Buffer> buffer =
            createLinearBuffer(block, 0, stat->write);

    /* All frames are SYNC FRAME */
    buffer->setInfo(std::make_shared<C2StreamPictureTypeMaskInfo::output>(
            0u /* stream id */, C2Config::SYNC_FRAME));

    auto fillWork = [buffer](const std::unique_ptr<C2Work> &work) {
        work->worklets.front()->output.flags = (C2FrameData::flags_t)0;
        work->worklets.front()->output.buffers.clear();
        work->worklets.front()->output.buffers.push_back(buffer);
        work->worklets.front()->output.ordinal = work->input.ordinal;
        work->workletsProcessed = 1u;
    };
    if (work && c2_cntr64_t(workIndex) == work->input.ordinal.frameIndex) {
        fillWork(work);
        if (mSignalledEos) {
            work->worklets.front()->output.flags = C2FrameData::FLAG_END_OF_STREAM;
        }
    } else {
        finish(workIndex, fillWork);
    }
}

c2_status_t C2SoftApvEnc::drainInternal(
        uint32_t drainMode,
        const std::shared_ptr<C2BlockPool> &pool,
        const std::unique_ptr<C2Work> &work) {
    fillEmptyWork(work);
    return C2_OK;
}

void C2SoftApvEnc::process(const std::unique_ptr<C2Work>& work,
                            const std::shared_ptr<C2BlockPool>& pool) {
    c2_status_t error;
    work->result = C2_OK;
    work->workletsProcessed = 0u;
    work->worklets.front()->output.flags = work->input.flags;

    nsecs_t timeDelay = 0;
    uint64_t workIndex = work->input.ordinal.frameIndex.peekull();
    
    mSignalledEos = false;
    mOutBlock = nullptr;

    if (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) {
        ALOGV("Got FLAG_END_OF_STREAM");
        mSignalledEos = true;
    }

    /* Initialize encoder if not already initialized */
    if (initEncoder() != C2_OK) {
        ALOGE("Failed to initialize encoder");
        mSignalledError = true;
        work->result = C2_CORRUPTED;
        work->workletsProcessed = 1u;
        ALOGE("[%s] Failed to make Codec context", __func__);
        return;
    }
    if (mSignalledError) {
        ALOGE("[%s] Received signalled error", __func__);
        return;
    }

    if (mSignalledEos) {
        drainInternal(DRAIN_COMPONENT_WITH_EOS, pool, work);
        return;
    }

    if (work->input.buffers.empty()) {
        return;
    }

    std::shared_ptr<C2GraphicView> view;
    std::shared_ptr<C2Buffer> inputBuffer = nullptr;
    if (!work->input.buffers.empty()) {
        inputBuffer = work->input.buffers[0];
        view = std::make_shared<C2GraphicView>(
                inputBuffer->data().graphicBlocks().front().map().get());
        if (view->error() != C2_OK) {
            ALOGE("graphic view map err = %d", view->error());
            work->workletsProcessed = 1u;
            return;
        }
    }
    if (!inputBuffer) {
        fillEmptyWork(work);
        return;
    }

    oapve_stat_t stat;
    auto outBufferSize = mCodecDesc->param[mReceivedFrames].w * mCodecDesc->param[mReceivedFrames].h * 4;
    if (!mOutBlock) {
        C2MemoryUsage usage = {C2MemoryUsage::CPU_READ,
                                C2MemoryUsage::CPU_WRITE};
        c2_status_t err =
            pool->fetchLinearBlock(outBufferSize, usage, &mOutBlock);
        if (err != C2_OK) {
            work->result = err;
            work->workletsProcessed = 1u;
            ALOGE("fetchLinearBlock has failed. err = %d", err);
            return;
        }
    }

    C2WriteView wView = mOutBlock->map().get();
    if (wView.error() != C2_OK) {
        work->result = wView.error();
        work->workletsProcessed = 1u;
        return;
    }

    error = setEncodeArgs(&InputFrames, view.get(), workIndex);
    if (error != C2_OK) {
        mSignalledError = true;
        work->result = error;
        work->workletsProcessed = 1u;
        return;
    }

    if (++mReceivedFrames < mMaxFrames) {
        return;
    }
    mReceivedFrames = 0;

    std::shared_ptr<oapv_bitb_t> bits = std::make_shared<oapv_bitb_t>();
    std::memset(mBitstreamBuf, 0, kMaxBitstreamBufSize);
    bits->addr = mBitstreamBuf;
    bits->bsize = kMaxBitstreamBufSize;
    bits->err = C2_OK;

    if (InputFrames.frm[0].imgb) {
        int32_t status = oapve_encode(mEncoderId, &InputFrames, mMetaId, bits.get(), &stat, &ReconFrames);
        if (status != C2_OK) {
            mSignalledError = true;
            work->result = C2_CORRUPTED;
            work->workletsProcessed = 1u;
            return;
        }
        ALOGV("Encoded %d bytes.", stat.write);
    }
    else if (!mSignalledEos) {
        fillEmptyWork(work);
    }
    finishWork(workIndex, work, pool, bits.get(), &stat);
}

class C2SoftApvEncFactory : public C2ComponentFactory {
   public:
    C2SoftApvEncFactory() : mHelper(std::static_pointer_cast<C2ReflectorHelper>(
              GetCodec2PlatformComponentStore()->getParamReflector())) {}

    virtual c2_status_t createComponent(
        c2_node_id_t id,
        std::shared_ptr<C2Component>* const component,
        std::function<void(C2Component*)> deleter) override {
        *component = std::shared_ptr<C2Component>(
            new C2SoftApvEnc(
                COMPONENT_NAME, id,
                std::make_shared<C2SoftApvEnc::IntfImpl>(mHelper)),
            deleter);
        return C2_OK;
    }

    c2_status_t createInterface(
        c2_node_id_t id,
        std::shared_ptr<C2ComponentInterface>* const interface,
        std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
            new SimpleInterface<C2SoftApvEnc::IntfImpl>(
                COMPONENT_NAME, id,
                std::make_shared<C2SoftApvEnc::IntfImpl>(mHelper)),
            deleter);
        return C2_OK;
    }

    ~C2SoftApvEncFactory() override = default;

   private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
};

}  // namespace android

__attribute__((cfi_canonical_jump_table))
extern "C" ::C2ComponentFactory* CreateCodec2Factory() {
    return new ::android::C2SoftApvEncFactory();
}

__attribute__((cfi_canonical_jump_table))
extern "C" void DestroyCodec2Factory(::C2ComponentFactory* factory) {
    delete factory;
}
