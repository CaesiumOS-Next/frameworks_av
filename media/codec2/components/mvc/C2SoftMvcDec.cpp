/*
 * Copyright 2017 The Android Open Source Project
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
#define LOG_TAG "C2SoftMvcDec"
#include <log/log.h>

#include <media/stagefright/foundation/MediaDefs.h>

#include <C2Debug.h>
#include <C2PlatformSupport.h>
#include <Codec2Mapper.h>
#include <SimpleC2Interface.h>

#include "C2SoftMvcDec.h"

namespace android {

extern const char *MEDIA_MIMETYPE_VIDEO_MVC;

namespace {
constexpr size_t kMinInputBufferSize = 2 * 1024 * 1024;
constexpr char COMPONENT_NAME[] = "c2.android.mvc.decoder";
constexpr uint32_t kDefaultOutputDelay = 8;
/* avc specification allows for a maximum delay of 16 frames.
   As soft avc decoder supports interlaced, this delay would be 32 fields.
   And avc decoder implementation has an additional delay of 2 decode calls.
   So total maximum output delay is 34 */
constexpr uint32_t kMaxOutputDelay = 34;
constexpr uint32_t kMinInputBytes = 4;
}  // namespace

class C2SoftAvcDec::IntfImpl : public SimpleInterface<void>::BaseParams {
public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper> &helper)
        : SimpleInterface<void>::BaseParams(
                helper,
                COMPONENT_NAME,
                C2Component::KIND_DECODER,
                C2Component::DOMAIN_VIDEO,
                MEDIA_MIMETYPE_VIDEO_AVC) {
        noPrivateBuffers(); // TODO: account for our buffers here
        noInputReferences();
        noOutputReferences();
        noInputLatency();
        noTimeStretch();

        // TODO: Proper support for reorder depth.
        addParameter(
                DefineParam(mActualOutputDelay, C2_PARAMKEY_OUTPUT_DELAY)
                .withDefault(new C2PortActualDelayTuning::output(kDefaultOutputDelay))
                .withFields({C2F(mActualOutputDelay, value).inRange(0, kMaxOutputDelay)})
                .withSetter(Setter<decltype(*mActualOutputDelay)>::StrictValueWithNoDeps)
                .build());

        // TODO: output latency and reordering

        addParameter(
                DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
                .withConstValue(new C2ComponentAttributesSetting(C2Component::ATTRIB_IS_TEMPORAL))
                .build());

        // coded and output picture size is the same for this codec
        addParameter(
                DefineParam(mSize, C2_PARAMKEY_PICTURE_SIZE)
                .withDefault(new C2StreamPictureSizeInfo::output(0u, 320, 240))
                .withFields({
                    C2F(mSize, width).inRange(2, 4080, 2),
                    C2F(mSize, height).inRange(2, 4080, 2),
                })
                .withSetter(SizeSetter)
                .build());

        addParameter(
                DefineParam(mMaxSize, C2_PARAMKEY_MAX_PICTURE_SIZE)
                .withDefault(new C2StreamMaxPictureSizeTuning::output(0u, 320, 240))
                .withFields({
                    C2F(mSize, width).inRange(2, 4080, 2),
                    C2F(mSize, height).inRange(2, 4080, 2),
                })
                .withSetter(MaxPictureSizeSetter, mSize)
                .build());

        addParameter(
                DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                .withDefault(new C2StreamProfileLevelInfo::input(0u,
                        C2Config::PROFILE_AVC_CONSTRAINED_BASELINE, C2Config::LEVEL_AVC_5_2))
                .withFields({
                    C2F(mProfileLevel, profile).oneOf({
                            C2Config::PROFILE_AVC_CONSTRAINED_BASELINE,
                            C2Config::PROFILE_AVC_BASELINE,
                            C2Config::PROFILE_AVC_MAIN,
                            C2Config::PROFILE_AVC_CONSTRAINED_HIGH,
                            C2Config::PROFILE_AVC_PROGRESSIVE_HIGH,
                            C2Config::PROFILE_AVC_HIGH}),
                    C2F(mProfileLevel, level).oneOf({
                            C2Config::LEVEL_AVC_1, C2Config::LEVEL_AVC_1B, C2Config::LEVEL_AVC_1_1,
                            C2Config::LEVEL_AVC_1_2, C2Config::LEVEL_AVC_1_3,
                            C2Config::LEVEL_AVC_2, C2Config::LEVEL_AVC_2_1, C2Config::LEVEL_AVC_2_2,
                            C2Config::LEVEL_AVC_3, C2Config::LEVEL_AVC_3_1, C2Config::LEVEL_AVC_3_2,
                            C2Config::LEVEL_AVC_4, C2Config::LEVEL_AVC_4_1, C2Config::LEVEL_AVC_4_2,
                            C2Config::LEVEL_AVC_5, C2Config::LEVEL_AVC_5_1, C2Config::LEVEL_AVC_5_2
                    })
                })
                .withSetter(ProfileLevelSetter, mSize)
                .build());

        addParameter(
                DefineParam(mMaxInputSize, C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE)
                .withDefault(new C2StreamMaxBufferSizeInfo::input(0u, kMinInputBufferSize))
                .withFields({
                    C2F(mMaxInputSize, value).any(),
                })
                .calculatedAs(MaxInputSizeSetter, mMaxSize)
                .build());

        C2ChromaOffsetStruct locations[1] = { C2ChromaOffsetStruct::ITU_YUV_420_0() };
        std::shared_ptr<C2StreamColorInfo::output> defaultColorInfo =
            C2StreamColorInfo::output::AllocShared(
                    1u, 0u, 8u /* bitDepth */, C2Color::YUV_420);
        memcpy(defaultColorInfo->m.locations, locations, sizeof(locations));

        defaultColorInfo =
            C2StreamColorInfo::output::AllocShared(
                    { C2ChromaOffsetStruct::ITU_YUV_420_0() },
                    0u, 8u /* bitDepth */, C2Color::YUV_420);
        helper->addStructDescriptors<C2ChromaOffsetStruct>();

        addParameter(
                DefineParam(mColorInfo, C2_PARAMKEY_CODED_COLOR_INFO)
                .withConstValue(defaultColorInfo)
                .build());

        addParameter(
                DefineParam(mDefaultColorAspects, C2_PARAMKEY_DEFAULT_COLOR_ASPECTS)
                .withDefault(new C2StreamColorAspectsTuning::output(
                        0u, C2Color::RANGE_UNSPECIFIED, C2Color::PRIMARIES_UNSPECIFIED,
                        C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
                .withFields({
                    C2F(mDefaultColorAspects, range).inRange(
                                C2Color::RANGE_UNSPECIFIED,     C2Color::RANGE_OTHER),
                    C2F(mDefaultColorAspects, primaries).inRange(
                                C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                    C2F(mDefaultColorAspects, transfer).inRange(
                                C2Color::TRANSFER_UNSPECIFIED,  C2Color::TRANSFER_OTHER),
                    C2F(mDefaultColorAspects, matrix).inRange(
                                C2Color::MATRIX_UNSPECIFIED,    C2Color::MATRIX_OTHER)
                })
                .withSetter(DefaultColorAspectsSetter)
                .build());

        addParameter(
                DefineParam(mCodedColorAspects, C2_PARAMKEY_VUI_COLOR_ASPECTS)
                .withDefault(new C2StreamColorAspectsInfo::input(
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
                .withSetter(CodedColorAspectsSetter)
                .build());

        addParameter(
                DefineParam(mColorAspects, C2_PARAMKEY_COLOR_ASPECTS)
                .withDefault(new C2StreamColorAspectsInfo::output(
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
                .withSetter(ColorAspectsSetter, mDefaultColorAspects, mCodedColorAspects)
                .build());

        // TODO: support more formats?
        addParameter(
                DefineParam(mPixelFormat, C2_PARAMKEY_PIXEL_FORMAT)
                .withConstValue(new C2StreamPixelFormatInfo::output(
                                     0u, HAL_PIXEL_FORMAT_YCBCR_420_888))
                .build());
    }
    static C2R SizeSetter(bool mayBlock, const C2P<C2StreamPictureSizeInfo::output> &oldMe,
                          C2P<C2StreamPictureSizeInfo::output> &me) {
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

    static C2R MaxPictureSizeSetter(bool mayBlock, C2P<C2StreamMaxPictureSizeTuning::output> &me,
                                    const C2P<C2StreamPictureSizeInfo::output> &size) {
        (void)mayBlock;
        // TODO: get max width/height from the size's field helpers vs. hardcoding
        me.set().width = c2_min(c2_max(me.v.width, size.v.width), 4080u);
        me.set().height = c2_min(c2_max(me.v.height, size.v.height), 4080u);
        return C2R::Ok();
    }

    static C2R MaxInputSizeSetter(bool mayBlock, C2P<C2StreamMaxBufferSizeInfo::input> &me,
                                  const C2P<C2StreamMaxPictureSizeTuning::output> &maxSize) {
        (void)mayBlock;
        // assume compression ratio of 2
        me.set().value = c2_max((((maxSize.v.width + 15) / 16)
                * ((maxSize.v.height + 15) / 16) * 192), kMinInputBufferSize);
        return C2R::Ok();
    }

    static C2R ProfileLevelSetter(bool mayBlock, C2P<C2StreamProfileLevelInfo::input> &me,
                                  const C2P<C2StreamPictureSizeInfo::output> &size) {
        (void)mayBlock;
        (void)size;
        (void)me;  // TODO: validate
        return C2R::Ok();
    }

    static C2R DefaultColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsTuning::output> &me) {
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

    static C2R CodedColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsInfo::input> &me) {
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

    static C2R ColorAspectsSetter(bool mayBlock, C2P<C2StreamColorAspectsInfo::output> &me,
                                  const C2P<C2StreamColorAspectsTuning::output> &def,
                                  const C2P<C2StreamColorAspectsInfo::input> &coded) {
        (void)mayBlock;
        // take default values for all unspecified fields, and coded values for specified ones
        me.set().range = coded.v.range == RANGE_UNSPECIFIED ? def.v.range : coded.v.range;
        me.set().primaries = coded.v.primaries == PRIMARIES_UNSPECIFIED
                ? def.v.primaries : coded.v.primaries;
        me.set().transfer = coded.v.transfer == TRANSFER_UNSPECIFIED
                ? def.v.transfer : coded.v.transfer;
        me.set().matrix = coded.v.matrix == MATRIX_UNSPECIFIED ? def.v.matrix : coded.v.matrix;
        return C2R::Ok();
    }

    std::shared_ptr<C2StreamColorAspectsInfo::output> getColorAspects_l() {
        return mColorAspects;
    }

private:
    std::shared_ptr<C2StreamProfileLevelInfo::input> mProfileLevel;
    std::shared_ptr<C2StreamPictureSizeInfo::output> mSize;
    std::shared_ptr<C2StreamMaxPictureSizeTuning::output> mMaxSize;
    std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mMaxInputSize;
    std::shared_ptr<C2StreamColorInfo::output> mColorInfo;
    std::shared_ptr<C2StreamColorAspectsInfo::input> mCodedColorAspects;
    std::shared_ptr<C2StreamColorAspectsTuning::output> mDefaultColorAspects;
    std::shared_ptr<C2StreamColorAspectsInfo::output> mColorAspects;
    std::shared_ptr<C2StreamPixelFormatInfo::output> mPixelFormat;
};

static size_t getCpuCoreCount() {
    long cpuCoreCount = 1;
#if defined(_SC_NPROCESSORS_ONLN)
    cpuCoreCount = sysconf(_SC_NPROCESSORS_ONLN);
#else
    // _SC_NPROC_ONLN must be defined...
    cpuCoreCount = sysconf(_SC_NPROC_ONLN);
#endif
    CHECK(cpuCoreCount >= 1);
    return (size_t)cpuCoreCount;
}

static void *ivd_aligned_malloc(void *ctxt, WORD32 alignment, WORD32 size) {
    (void) ctxt;
    return memalign(alignment, size);
}

static void ivd_aligned_free(void *ctxt, void *mem) {
    (void) ctxt;
    free(mem);
}

C2SoftAvcDec::C2SoftAvcDec(
        const char *name,
        c2_node_id_t id,
        const std::shared_ptr<IntfImpl> &intfImpl)
    : SimpleC2Component(std::make_shared<SimpleInterface<IntfImpl>>(name, id, intfImpl)),
      mIntf(intfImpl),
      mDecHandle(nullptr),
      mIsHeaderReady(false),
      mIsSliceReady(false),
      mIsFlushMode(false),
      mIvColorFormat(IV_YUV_420P),
      mOutputDelay(kDefaultOutputDelay),
      frameNum(0),
      secondFrame(false),
      mWidth(1920),
      mHeight(1080),
      mHeaderDecoded(false),
      mOutIndex(0u) {
    GENERATE_FILE_NAMES();
    CREATE_DUMP_FILE(mInFile);
}

C2SoftAvcDec::~C2SoftAvcDec() {
    onRelease();
}

c2_status_t C2SoftAvcDec::onInit() {
    status_t err = initDecoder();
    return err == OK ? C2_OK : C2_CORRUPTED;
}

c2_status_t C2SoftAvcDec::onStop() {
    if (OK != resetDecoder()) return C2_CORRUPTED;
    //if (OK != resetState()) return C2_CORRUPTED;
    resetPlugin();
    return C2_OK;
}

void C2SoftAvcDec::onReset() {
    (void) onStop();
}

void C2SoftAvcDec::onRelease() {
    (void) deleteDecoder();
    uint32_t i;
    for(i = 0; i< MAX_NUM_VIEWS; i++){
        if (mOutBufferFlush[i]) {
            ivd_aligned_free(nullptr, mOutBufferFlush[i]);
            mOutBufferFlush[i] = nullptr;
        }
    }
    
    if (mOutBlock) {
        mOutBlock.reset();
    }
}

c2_status_t C2SoftAvcDec::onFlush_sm() {
    if (OK != setFlushMode()) return C2_CORRUPTED;

    uint32_t i;
    uint32_t bufferSize = mStride * mHeight * 2;
    for(i = 0; i < MAX_NUM_VIEWS; i++) {
        mOutBufferFlush[i] = (uint8_t *)ivd_aligned_malloc(nullptr, 128, bufferSize);
            
        if (!mOutBufferFlush[i]) {
            ALOGE("could not allocate tmp output buffer %u (for flush) of size %u ", i, bufferSize);
            return C2_NO_MEMORY;
        }
    }

    while (true) {
        imvcd_video_decode_ip_t s_decode_ip;
        imvcd_video_decode_op_t s_decode_op;

        imvcd_video_decode_ip_t *ps_decode_ip = &s_decode_ip;
        imvcd_video_decode_op_t *ps_decode_op = &s_decode_op;

        setDecodeArgs(ps_decode_ip, ps_decode_op, nullptr, nullptr, nullptr, 0, 0, 0);
      
        (void) ivdec_api_function(mDecHandle, ps_decode_ip, ps_decode_op);
        if (0 == ps_decode_op->s_ivd_op.u4_output_present) {
            resetPlugin();
            break;
        }
    }

    for(i = 0; i < MAX_NUM_VIEWS; i++) {
        if (mOutBufferFlush[i]) {
            ivd_aligned_free(nullptr, mOutBufferFlush[i]);
            mOutBufferFlush[i] = nullptr;
        }
    }

    return C2_OK;
}

status_t C2SoftAvcDec::createDecoder() {
    imvcd_create_ip_t s_create_ip;
    imvcd_create_op_t s_create_op;
    IV_API_CALL_STATUS_T status;

    s_create_ip.s_ivd_ip.u4_size = sizeof(imvcd_create_ip_t);
    s_create_ip.s_ivd_ip.e_cmd = IVD_CMD_CREATE;
    s_create_ip.s_ivd_ip.u4_share_disp_buf = 0;
    s_create_ip.s_ivd_ip.e_output_format = mIvColorFormat;
    s_create_ip.s_ivd_ip.pf_aligned_alloc = ivd_aligned_malloc;
    s_create_ip.s_ivd_ip.pf_aligned_free = ivd_aligned_free;
    s_create_ip.s_ivd_ip.pv_mem_ctxt = nullptr;
    s_create_op.s_ivd_op.u4_size = sizeof(imvcd_create_op_t);

    status = ivdec_api_function(nullptr,
                                 &s_create_ip,
                                 &s_create_op);

    if (status != IV_SUCCESS) {
        ALOGE("error in %s: 0x%x", __func__, s_create_op.s_ivd_op.u4_error_code);
        return UNKNOWN_ERROR;
    }

    mDecHandle = (iv_obj_t*)s_create_op.s_ivd_op.pv_handle;
    mDecHandle->u4_size = sizeof(iv_obj_t);
    ALOGV("CREATE DECODER!");

    return OK;
}

status_t C2SoftAvcDec::setNumCores() {
    imvcd_set_num_cores_ip_t s_ctl_ip;
    imvcd_set_num_cores_op_t s_ctl_op;
    IV_API_CALL_STATUS_T status;

    s_ctl_ip.u4_size = sizeof(s_ctl_ip);
    s_ctl_op.u4_size = sizeof(s_ctl_op);
    s_ctl_ip.u4_num_cores = 1;//mNumCores;
    s_ctl_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_ctl_ip.e_sub_cmd = (IVD_CONTROL_API_COMMAND_TYPE_T) IMVCD_CTL_SET_NUM_CORES;

    status = ivdec_api_function(mDecHandle, &s_ctl_ip, &s_ctl_op);

    if (IV_SUCCESS != status) {
        ALOGD("error in %s: 0x%x", __func__, s_ctl_op.u4_error_code);
        return UNKNOWN_ERROR;
    }

    return OK;
}

status_t C2SoftAvcDec::setParams(size_t stride, IVD_VIDEO_DECODE_MODE_T dec_mode) {
    imvcd_set_config_ip_t s_ctl_ip;
    imvcd_set_config_op_t s_ctl_op;
    IV_API_CALL_STATUS_T status;

    s_ctl_ip.s_ivd_ip.u4_size = sizeof(s_ctl_ip.s_ivd_ip);
    s_ctl_op.s_ivd_op.u4_size = sizeof(s_ctl_op.s_ivd_op);
    s_ctl_ip.s_ivd_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_ctl_ip.s_ivd_ip.e_sub_cmd = (IVD_CONTROL_API_COMMAND_TYPE_T) IVD_CMD_CTL_SETPARAMS;
    s_ctl_ip.s_ivd_ip.u4_disp_wd = (UWORD32) stride;
    s_ctl_ip.s_ivd_ip.e_frm_out_mode = IVD_DISPLAY_FRAME_OUT;
    s_ctl_ip.s_ivd_ip.e_frm_skip_mode = IVD_SKIP_NONE;
    s_ctl_ip.s_ivd_ip.e_vid_dec_mode = dec_mode;

    status = ivdec_api_function(mDecHandle, &s_ctl_ip, &s_ctl_op);

    if (status != IV_SUCCESS) {
        ALOGE("error in %s: 0x%x", __func__, s_ctl_op.s_ivd_op.u4_error_code);
        return UNKNOWN_ERROR;
    }

    return OK;
}

status_t C2SoftAvcDec::initDecoder() {
    if (OK != createDecoder()) return UNKNOWN_ERROR;
    mNumCores = MIN(getCpuCoreCount(), MAX_NUM_CORES);
    mStride = ALIGN128(mWidth);
    mSignalledError = false;
    resetPlugin();
    (void) setNumCores();
    if (OK != setParams(mStride, IVD_DECODE_HEADER)) return UNKNOWN_ERROR;
    //if (OK != resetState()) return UNKNOWN_ERROR;

    return OK;
}

status_t C2SoftAvcDec::outBufAlloc(ivd_out_bufdesc_t *ps_out_buf, C2GraphicView *wView) {
    UNUSED(wView);
    UWORD32 i;

    UWORD16 u2_num_views = mDispBufProps.s_mvc_buf_info.u2_num_views;

    if(mDispBufProps.s_ivd_op.u4_min_num_out_bufs < (NUM_COMPONENTS * u2_num_views))
    {
        return IV_FAIL;
    }

    ps_out_buf->u4_num_bufs = mDispBufProps.s_ivd_op.u4_min_num_out_bufs;

    for(i = 0; i < ps_out_buf->u4_num_bufs; i++)
    {
        ps_out_buf->u4_min_out_buf_size[i] =
            mDispBufProps.s_ivd_op.u4_min_out_buf_size[i];
        ps_out_buf->pu1_bufs[i] =
            (UWORD8 *) malloc(mDispBufProps.s_ivd_op.u4_min_out_buf_size[i]);

        if(ps_out_buf->pu1_bufs[i] == NULL)
        {
            return IV_FAIL;
        }
    }

    return IV_SUCCESS;
}

status_t C2SoftAvcDec::getBufInfo(C2GraphicView *wView) {
    imvcd_get_buf_info_ip_t s_ctl_ip;
    imvcd_get_buf_info_op_t s_ctl_op;
    IV_API_CALL_STATUS_T status;

    s_ctl_ip.s_ivd_ip.u4_size = sizeof(s_ctl_ip.s_ivd_ip);
    s_ctl_op.s_ivd_op.u4_size = sizeof(s_ctl_op.s_ivd_op);
    s_ctl_ip.s_ivd_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_ctl_ip.s_ivd_ip.e_sub_cmd = (IVD_CONTROL_API_COMMAND_TYPE_T) IVD_CMD_CTL_GETBUFINFO;

    status = ivdec_api_function(mDecHandle, &s_ctl_ip, &s_ctl_op);

    if(status != IV_SUCCESS){
        ALOGE("error in %s: 0x%x", __func__, s_ctl_op.s_ivd_op.u4_error_code);
        return UNKNOWN_ERROR;
    }

    mDispBufProps = s_ctl_op;
    outBufAlloc(&s_out_buf, wView);

    return status;
}

bool C2SoftAvcDec::setDecodeArgs(imvcd_video_decode_ip_t *ps_decode_ip,
                                 imvcd_video_decode_op_t *ps_decode_op,
                                 ivd_out_bufdesc_t *ps_out_buf,
                                 C2ReadView *inBuffer,
                                 C2GraphicView *outBuffer,
                                 size_t inOffset,
                                 size_t inSize,
                                 uint32_t tsMarker) {
    uint32_t i;
    //iv_mvc_yuv_buf_t s_disp_buf;
    uint32_t displayStride = mStride;
    uint32_t numViews = mDispBufProps.s_mvc_buf_info.u2_num_views;
    //uint32_t numSlices = 0;
    ps_decode_ip->s_ivd_ip.u4_size = sizeof(imvcd_video_decode_ip_t);
    ps_decode_ip->s_ivd_ip.e_cmd = IVD_CMD_VIDEO_DECODE;

    if (outBuffer) {
        C2PlanarLayout layout;
        layout = outBuffer->layout();
        displayStride = layout.planes[C2PlanarLayout::PLANE_Y].rowInc;
    }

    uint32_t displayHeight = mHeight;
    size_t lumaSize = displayStride * displayHeight;
    size_t chromaSize = lumaSize >> 2;

    if (mStride != displayStride) {
        mStride = displayStride;
    }

    if (inBuffer) {
        ps_decode_ip->s_ivd_ip.u4_ts = tsMarker;
        ps_decode_ip->s_ivd_ip.pv_stream_buffer = const_cast<uint8_t *>(inBuffer->data() + inOffset);
        ps_decode_ip->s_ivd_ip.u4_num_Bytes = inSize;
        ps_decode_ip->s_ivd_ip.s_out_buffer.pu1_bufs[0] = mOutBuf[0];
        ps_decode_ip->s_ivd_ip.s_out_buffer.pu1_bufs[1] = mOutBuf[0] + lumaSize;
        ps_decode_ip->s_ivd_ip.s_out_buffer.pu1_bufs[2] = mOutBuf[0] + lumaSize + (lumaSize >> 2);
    } else {
        ps_decode_ip->s_ivd_ip.u4_ts = 0;
        ps_decode_ip->s_ivd_ip.pv_stream_buffer = nullptr;
        ps_decode_ip->s_ivd_ip.u4_num_Bytes = 0;
    }

    if (outBuffer) {
        if (outBuffer->height() < displayHeight) {
            ALOGE("Output buffer too small: provided (%dx%d) required (%ux%u)",
                  outBuffer->width(), outBuffer->height(), displayStride, displayHeight);
            return false;
        }

        ps_decode_ip->s_ivd_ip.s_out_buffer = ps_out_buf[0];
        ps_decode_op->ps_view_disp_bufs = as_view_disp_bufs;
        
        ps_decode_op->s_ivd_op.u4_size = sizeof(imvcd_video_decode_op_t);
    } else {
        ps_decode_op->ps_view_disp_bufs = as_view_disp_bufs;
        ps_decode_op->s_ivd_op.u4_size = sizeof(imvcd_video_decode_op_t);

        for(i = 0; i < numViews; i++)
        {
            ps_decode_ip->s_ivd_ip.s_out_buffer.pu1_bufs[0] = mOutBufferFlush[i];
            ps_decode_ip->s_ivd_ip.s_out_buffer.pu1_bufs[1] = mOutBufferFlush[i] + lumaSize;
            ps_decode_ip->s_ivd_ip.s_out_buffer.pu1_bufs[2] = mOutBufferFlush[i] + lumaSize + chromaSize;
        }
    }

    return true;
}

status_t C2SoftAvcDec::setFlushMode() {
    imvcd_flush_dec_ip_t s_ctl_ip;
    imvcd_flush_dec_op_t s_ctl_op;

    IV_API_CALL_STATUS_T status;

    s_ctl_ip.s_ivd_ip.u4_size = sizeof(imvcd_flush_dec_ip_t);
    s_ctl_ip.s_ivd_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_ctl_ip.s_ivd_ip.e_sub_cmd = (IVD_CONTROL_API_COMMAND_TYPE_T) IVD_CMD_CTL_FLUSH;
    s_ctl_op.s_ivd_op.u4_size = sizeof(imvcd_flush_dec_op_t);

    status = ivdec_api_function(mDecHandle, &s_ctl_ip, &s_ctl_op);

    if (status != IV_SUCCESS) {
        ALOGE("error in %s: 0x%x", __func__, s_ctl_op.s_ivd_op.u4_error_code);
        return UNKNOWN_ERROR;
    }

    mIsFlushMode = true;

    return OK;
}

status_t C2SoftAvcDec::resetDecoder() {
    /* Delete Decoder and Recreate */
    if (OK != deleteDecoder()) return UNKNOWN_ERROR;
        
    if (OK != createDecoder()) return UNKNOWN_ERROR;

    mStride = 0;
    (void) setNumCores();
    mSignalledError = false;
    mHeaderDecoded = false;

    //if (OK != resetState()) return UNKNOWN_ERROR;

    return OK;
}

void C2SoftAvcDec::resetPlugin() {
    mSignalledOutputEos = false;
    gettimeofday(&mTimeStart, nullptr);
    gettimeofday(&mTimeEnd, nullptr);
    if (mOutBlock) {
        mOutBlock.reset();
    }

}

status_t C2SoftAvcDec::deleteDecoder() {
    if (mDecHandle) {
        IV_API_CALL_STATUS_T status;
        ivdext_delete_ip_t s_delete_ip = {};
        ivdext_delete_op_t s_delete_op = {};

        s_delete_ip.s_ivd_delete_ip_t.u4_size = sizeof(ivdext_delete_ip_t);
        s_delete_ip.s_ivd_delete_ip_t.e_cmd = IVD_CMD_DELETE;
        s_delete_op.s_ivd_delete_op_t.u4_size = sizeof(ivdext_delete_op_t);
        
        status = ivdec_api_function(mDecHandle,
                                     &s_delete_ip,
                                     &s_delete_op);
        
        if(status != IV_SUCCESS){  
            ALOGE("error in %s", __func__);
            return UNKNOWN_ERROR;
        }
        mDecHandle = nullptr;
    }

    return OK;
}

bool C2SoftAvcDec::getVuiParams() {
    ivdext_ctl_get_vui_params_ip_t s_ctl_ip;
    ivdext_ctl_get_vui_params_op_t s_ctl_op;
    IV_API_CALL_STATUS_T status;
    s_ctl_ip.u4_size = sizeof(ivdext_ctl_get_vui_params_ip_t);
    s_ctl_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_ctl_ip.e_sub_cmd = (IVD_CONTROL_API_COMMAND_TYPE_T) IMVCD_CTL_GET_VUI_PARAMS;
    s_ctl_op.u4_size = sizeof(ivdext_ctl_get_vui_params_op_t);

    status = ivdec_api_function(mDecHandle, &s_ctl_ip, &s_ctl_op);

    if(status != IV_SUCCESS){
        ALOGE("error in %s: 0x%x", __func__, s_ctl_op.u4_error_code);
        return UNKNOWN_ERROR;
    }
    
    VuiColorAspects vuiColorAspects;

    if (s_ctl_op.b_is_vui_available ) {
        vuiColorAspects.primaries = s_ctl_op.u1_colour_primaries;
        vuiColorAspects.transfer = s_ctl_op.u1_tfr_chars;
        vuiColorAspects.coeffs = s_ctl_op.u1_matrix_coeffs;
        vuiColorAspects.fullRange = s_ctl_op.u1_video_full_range_flag;
    }
    

    // convert vui aspects to C2 values if changed
    if (!(vuiColorAspects == mBitstreamColorAspects)) {
        mBitstreamColorAspects = vuiColorAspects;
        ColorAspects sfAspects;
        C2StreamColorAspectsInfo::input codedAspects = { 0u };
        ColorUtils::convertIsoColorAspectsToCodecAspects(
                vuiColorAspects.primaries, vuiColorAspects.transfer, vuiColorAspects.coeffs,
                vuiColorAspects.fullRange, sfAspects);
        if (!C2Mapper::map(sfAspects.mPrimaries, &codedAspects.primaries)) {
            codedAspects.primaries = C2Color::PRIMARIES_UNSPECIFIED;
        }
        if (!C2Mapper::map(sfAspects.mRange, &codedAspects.range)) {
            codedAspects.range = C2Color::RANGE_UNSPECIFIED;
        }
        if (!C2Mapper::map(sfAspects.mMatrixCoeffs, &codedAspects.matrix)) {
            codedAspects.matrix = C2Color::MATRIX_UNSPECIFIED;
        }
        if (!C2Mapper::map(sfAspects.mTransfer, &codedAspects.transfer)) {
            codedAspects.transfer = C2Color::TRANSFER_UNSPECIFIED;
        }
        std::vector<std::unique_ptr<C2SettingResult>> failures;
        (void)mIntf->config({&codedAspects}, C2_MAY_BLOCK, &failures);
    }
    return true;
}


static void fillEmptyWork(const std::unique_ptr<C2Work> &work) {
    uint32_t flags = 0;
    if (work->input.flags & C2FrameData::FLAG_END_OF_STREAM) {
        flags |= C2FrameData::FLAG_END_OF_STREAM;
    }
    work->worklets.front()->output.flags = (C2FrameData::flags_t)flags;
    work->worklets.front()->output.buffers.clear();
    work->worklets.front()->output.ordinal = work->input.ordinal;
    work->workletsProcessed = 1u;
}

void C2SoftAvcDec::finishWork(uint64_t index, const std::unique_ptr<C2Work> &work) {
    std::shared_ptr<C2Buffer> buffer = createGraphicBuffer(std::move(mOutBlock),
                                                           C2Rect(mWidth, mHeight));
    mOutBlock = nullptr;
    {
        IntfImpl::Lock lock = mIntf->lock();
        buffer->setInfo(mIntf->getColorAspects_l());
    }

    class FillWork {
       public:
        FillWork(uint32_t flags, C2WorkOrdinalStruct ordinal,
                 const std::shared_ptr<C2Buffer>& buffer)
            : mFlags(flags), mOrdinal(ordinal), mBuffer(buffer) {}
        ~FillWork() = default;

        void operator()(const std::unique_ptr<C2Work>& work) {
            work->worklets.front()->output.flags = (C2FrameData::flags_t)mFlags;
            work->worklets.front()->output.buffers.clear();
            work->worklets.front()->output.ordinal = mOrdinal;
            work->workletsProcessed = 1u;
            work->result = C2_OK;
            if (mBuffer) {
                work->worklets.front()->output.buffers.push_back(mBuffer);
            }
            ALOGV("timestamp = %lld, index = %lld, w/%s buffer",
                  mOrdinal.timestamp.peekll(), mOrdinal.frameIndex.peekll(),
                  mBuffer ? "" : "o");
        }

       private:
        const uint32_t mFlags;
        const C2WorkOrdinalStruct mOrdinal;
        const std::shared_ptr<C2Buffer> mBuffer;
    };

    auto fillWork = [buffer](const std::unique_ptr<C2Work> &work) {
        work->worklets.front()->output.flags = (C2FrameData::flags_t)0;
        work->worklets.front()->output.buffers.clear();
        work->worklets.front()->output.buffers.push_back(buffer);
        work->worklets.front()->output.ordinal = work->input.ordinal;
        work->workletsProcessed = 1u;
    };
    if (work && c2_cntr64_t(index) == work->input.ordinal.frameIndex) {
        bool eos = ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0);
        // TODO: Check if cloneAndSend can be avoided by tracking number of frames remaining
        if (eos) {
            if (buffer) {
                mOutIndex = index;
                C2WorkOrdinalStruct outOrdinal = work->input.ordinal;
                cloneAndSend(
                    mOutIndex, work,
                    FillWork(C2FrameData::FLAG_INCOMPLETE, outOrdinal, buffer));
                buffer.reset();
            }
        } else {
            fillWork(work);
        }
    } else {
        finish(index, fillWork);
    }
}

c2_status_t C2SoftAvcDec::ensureDecoderState(const std::shared_ptr<C2BlockPool> &pool) {
    if (!mDecHandle) {
        ALOGE("not supposed to be here, invalid decoder context");
        return C2_CORRUPTED;
    }
    if (mOutBlock &&
            (mOutBlock->width() != ALIGN128(mWidth) || mOutBlock->height() != mHeight)) {
        mOutBlock.reset();
    }
    if (!mOutBlock) {
        uint32_t format = HAL_PIXEL_FORMAT_YV12;
        C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
        c2_status_t err =
            pool->fetchGraphicBlock(ALIGN128(mWidth), mHeight, format, usage, &mOutBlock);
        if (err != C2_OK) {
            ALOGE("fetchGraphicBlock for Output failed with status %d", err);
            return err;
        }
        ALOGV("provided (%dx%d) required (%dx%d)",
              mOutBlock->width(), mOutBlock->height(), ALIGN128(mWidth), mHeight);
    }

    return C2_OK;
}

void C2SoftAvcDec::stitchViewsNew(C2GraphicView *wView) {
    uint8_t *outData_Y = (uint8_t*) wView->data()[C2PlanarLayout::PLANE_Y];
    uint8_t *outData_U = (uint8_t*) wView->data()[C2PlanarLayout::PLANE_U];
    uint8_t *outData_V = (uint8_t*) wView->data()[C2PlanarLayout::PLANE_V];

    uint32_t numViews = mDispBufProps.s_mvc_buf_info.u2_num_views;
    
    if (1 == numViews) {
        memcpy(outData_Y, mOutBuf[0], mWidth * mHeight);
        memcpy(outData_U, mOutBuf[0] + mWidth * mHeight, mWidth * mHeight / 4);
        memcpy(outData_V, mOutBuf[0] + mWidth * mHeight + (mWidth * mHeight / 4), mWidth * mHeight / 4);
    } else {
        uint32_t view_id;
        uint32_t row_start_offset, col_start_offset, num_views_in_row, num_views_in_col, pad_offset_row, pad_offset_col;
        uint32_t regular_view_width, regular_view_height;
        uint8_t *src, *dst;

        if (6 == numViews) {
            num_views_in_row = 3;
            num_views_in_col = 2;
        } else {
            num_views_in_row = numViews;
            num_views_in_col = 1;
        }
        
        regular_view_width = mWidth / num_views_in_row;
        // setting output width multiple of 32
        regular_view_width &= 0xFFFFFFE0;

        regular_view_height = mHeight / num_views_in_col;
        // setting output height multiple of 64
        regular_view_height &= 0xFFFFFFC0;

        pad_offset_row = (mWidth - (num_views_in_row * regular_view_width)) / 2;
        pad_offset_col = (mHeight - (num_views_in_col * regular_view_height)) / 2;

        uint8_t *tmpScaledBuf = (uint8_t *)ivd_aligned_malloc(nullptr, 32, mWidth * mHeight * 2);

        ALOGV("STARTING FOR LOOP WITH PARAM :\nregular_view_width : %u\nregular_view_height : %u\npad_offset_row : %u\npad_offset_col : %u",
        regular_view_width,regular_view_height,pad_offset_row,pad_offset_col);

        for (view_id = 0; view_id < numViews; view_id++) {
            row_start_offset = 0;//view_id / num_views_in_row;
            col_start_offset = 0;//view_id % num_views_in_row;		

            if(numViews == 6) {
                //Y
                src = mOutBuf[view_id];
                dst = outData_Y + pad_offset_row + (pad_offset_col * mWidth) +
                    (col_start_offset * regular_view_width) + (row_start_offset * mWidth *
                    regular_view_height);

                ivdext_horizontal_downscale_and_transpose(src, mWidth, tmpScaledBuf, mWidth, regular_view_width, mHeight, 3);

                ivdext_horizontal_downscale_and_transpose(tmpScaledBuf, mWidth, dst, mWidth, regular_view_height, regular_view_width, 2);

                //U
                src = mOutBuf[view_id] + mWidth * mHeight;
                dst = outData_U + (pad_offset_row / 2) + (pad_offset_col * mWidth / 4)  +
                    (col_start_offset * regular_view_width / 2) + (row_start_offset * mWidth *
                    regular_view_height / 4);

                ivdext_horizontal_downscale_and_transpose(src, mWidth / 2, tmpScaledBuf, mWidth / 2, regular_view_width / 2, mHeight / 2, 3);
                ivdext_horizontal_downscale_and_transpose(tmpScaledBuf, mWidth / 2, dst, mWidth / 2, regular_view_height / 2, regular_view_width / 2, 2);

                //V
                src = mOutBuf[view_id] + (mWidth * mHeight) + (mWidth * mHeight / 4);
                dst = outData_V + (pad_offset_row / 2) + (pad_offset_col * mWidth / 4) +
                    (col_start_offset * regular_view_width / 2) + (row_start_offset * mWidth *
                    regular_view_height / 4);
                
                ivdext_horizontal_downscale_and_transpose(src, mWidth / 2, tmpScaledBuf, mWidth / 2, regular_view_width / 2, mHeight / 2, 3);
                ivdext_horizontal_downscale_and_transpose(tmpScaledBuf, mWidth / 2, dst, mWidth / 2, regular_view_height / 2, regular_view_width / 2, 2);
                 
            } else {
                //Y
                src = mOutBuf[0];
                dst = outData_Y + pad_offset_row + (pad_offset_col * mWidth) + (col_start_offset * regular_view_width) + (row_start_offset * mWidth * regular_view_height);

                ivdext_horizontal_downscale_no_transpose(src, mWidth, dst, mWidth, regular_view_width, regular_view_height, numViews);

                //U
                src = mOutBuf[0] + mWidth * mHeight;
                dst = outData_U + (pad_offset_row / 2) + (pad_offset_col * mWidth / 4) + (col_start_offset * regular_view_width / 2) + (row_start_offset * mWidth * regular_view_height / 4);

                ivdext_horizontal_downscale_no_transpose(src, mWidth / 2, dst, mWidth / 2, regular_view_width / 2, regular_view_height / 2, numViews);

                //V
                src = mOutBuf[0] + (mWidth * mHeight) + (mWidth * mHeight / 4) ;
                dst = outData_V + (pad_offset_row / 2) + (pad_offset_col * mWidth / 4) + (col_start_offset * regular_view_width / 2) + (row_start_offset * mWidth * regular_view_height / 4);

                ivdext_horizontal_downscale_no_transpose(src, mWidth / 2, dst, mWidth / 2, regular_view_width / 2, regular_view_height / 2, numViews);

            }
        }

        // paint black pillers if needed
        if (pad_offset_row)
        {
            // Y
            UWORD8 *dst_start = outData_Y;
            UWORD8 *dst_end = dst_start + (num_views_in_row * regular_view_width) + pad_offset_row;
            for (int i = 0; i < mHeight; i++)
            {
                memset(dst_start, 0, pad_offset_row);
                memset(dst_end, 0, pad_offset_row);
                dst_start += mWidth;
                dst_end += mWidth;
            }

            // U
            dst_start = outData_U;
            dst_end = dst_start + (num_views_in_row * regular_view_width / 2) + (pad_offset_row / 2);

            for (int i = 0; i < mHeight / 2; i++)
            {
                memset(dst_start, 128, pad_offset_row / 2);
                memset(dst_end, 128, pad_offset_row / 2);
                dst_start += mWidth / 2;
                dst_end += mWidth / 2;
            }

            // V
            dst_start = outData_V;
            dst_end = dst_start + (num_views_in_row * regular_view_width / 2) + (pad_offset_row / 2);

            for (int i = 0; i < mHeight / 2; i++)
            {
                memset(dst_start, 128, pad_offset_row / 2);
                memset(dst_end, 128, pad_offset_row / 2);
                dst_start += mWidth / 2;
                dst_end += mWidth / 2;
            }
        }

        // Bring the views to center
        if(pad_offset_col)
        {
            // Y
            UWORD8 *dst_start = outData_Y;
            UWORD8 *dst_end = dst_start + (pad_offset_col * mWidth) +
                        (num_views_in_col * regular_view_height * mWidth);

            memset(dst_start, 0, mWidth * pad_offset_col);
            memset(dst_end, 0, mWidth * pad_offset_col);
            // U
            dst_start = outData_U;
            dst_end = dst_start + (pad_offset_col * mWidth / 4) +
                        (num_views_in_col * regular_view_height * mWidth / 4);

            memset(dst_start, 128, mWidth * pad_offset_col / 4);
            memset(dst_end, 128, mWidth * pad_offset_col / 4);

            // V
            dst_start = outData_V;
            dst_end = dst_start + (pad_offset_col * mWidth / 4) +
                        (num_views_in_col * regular_view_height * mWidth / 4);

            memset(dst_start, 128, mWidth * pad_offset_col / 4);
            memset(dst_end, 128, mWidth * pad_offset_col / 4);
        }
        free(tmpScaledBuf);            
    }
}
int i=0;
void C2SoftAvcDec::stitchViews(C2GraphicView *wView) {
    uint8_t *outData_Y = (uint8_t*) wView->data()[C2PlanarLayout::PLANE_Y];
    uint8_t *outData_U = (uint8_t*) wView->data()[C2PlanarLayout::PLANE_U];
    uint8_t *outData_V = (uint8_t*) wView->data()[C2PlanarLayout::PLANE_V];
    uint32_t numViews = mDispBufProps.s_mvc_buf_info.u2_num_views;
    uint32_t lumaSize = mWidth * mHeight;
    uint32_t chromaSize = lumaSize >> 2;

    uint8_t *out_y = (uint8_t *)as_view_disp_bufs[0].pv_y_buf;
    uint8_t *out_u = (uint8_t *)as_view_disp_bufs[0].pv_u_buf;
    uint8_t *out_v = (uint8_t *)as_view_disp_bufs[0].pv_v_buf;
    uint32_t u4_y_strd = as_view_disp_bufs[0].u4_y_strd;
    if (1 == numViews) {
        for(i=0;i<mHeight;i++) {
            memcpy(outData_Y, out_y, mWidth);
            outData_Y+=mStride;
            out_y+=mWidth;
        }
        for(i=0;i<mHeight/2;i++) {
            memcpy(outData_U, out_u, mWidth/2);
            memcpy(outData_V, out_v, mWidth/2);
            outData_U+=mStride/2;
            out_u+=mWidth/2;
            outData_V+=mStride/2;
            out_v+=mWidth/2;
        }
    } else {
        uint32_t view_id;
        uint32_t row_start_offset, col_start_offset, num_views_in_row, num_views_in_col, pad_offset_row, pad_offset_col;
        uint32_t regular_view_width, regular_view_height;
        uint8_t *src, *dst;

        if (6 == numViews) {
            num_views_in_row = 3;
            num_views_in_col = 2;
        } else {
            num_views_in_row = numViews;
            num_views_in_col = 1;
        }
        
        regular_view_width = mWidth / num_views_in_row;
        // setting output width multiple of 32
        regular_view_width &= 0xFFFFFFE0;

        regular_view_height = mHeight / num_views_in_col;
        // setting output height multiple of 64
        regular_view_height &= 0xFFFFFFC0;

        pad_offset_row = (mWidth - (num_views_in_row * regular_view_width)) / 2;
        pad_offset_col = (mHeight - (num_views_in_col * regular_view_height)) / 2;

        uint8_t *tmpScaledBuf = (uint8_t *)ivd_aligned_malloc(nullptr, 32, mWidth * mHeight * 2);

        ALOGV("STARTING FOR LOOP WITH PARAM :\nregular_view_width : %u\nregular_view_height : %u\npad_offset_row : %u\npad_offset_col : %u",
        regular_view_width,regular_view_height,pad_offset_row,pad_offset_col);

        for (view_id = 0; view_id < numViews; view_id++) {
            row_start_offset = view_id / num_views_in_row;
            col_start_offset = view_id % num_views_in_row;		

            if(numViews == 6) {
                //Y
                src = (uint8_t *)as_view_disp_bufs[view_id].pv_y_buf;
                dst = outData_Y + pad_offset_row + (pad_offset_col * mWidth) +
                    (col_start_offset * regular_view_width) + (row_start_offset * mWidth *
                    regular_view_height);

                ivdext_horizontal_downscale_and_transpose(src, mWidth, tmpScaledBuf, mWidth, regular_view_width, mHeight, 3);
                ivdext_horizontal_downscale_and_transpose(tmpScaledBuf, mWidth, dst, mWidth, regular_view_height, regular_view_width, 2);

                //U
                src = (uint8_t *)as_view_disp_bufs[view_id].pv_u_buf;
                dst = outData_U + (pad_offset_row / 2) + (pad_offset_col * mWidth / 4)  +
                    (col_start_offset * regular_view_width / 2) + (row_start_offset * mWidth *
                    regular_view_height / 4);

                ivdext_horizontal_downscale_and_transpose(src, mWidth / 2, tmpScaledBuf, mWidth / 2, regular_view_width / 2, mHeight / 2, 3);
                ivdext_horizontal_downscale_and_transpose(tmpScaledBuf, mWidth / 2, dst, mWidth / 2, regular_view_height / 2, regular_view_width / 2, 2);

                //V
                src = (uint8_t *)as_view_disp_bufs[view_id].pv_v_buf;
                dst = outData_V + (pad_offset_row / 2) + (pad_offset_col * mWidth / 4) +
                    (col_start_offset * regular_view_width / 2) + (row_start_offset * mWidth *
                    regular_view_height / 4);
                
                ivdext_horizontal_downscale_and_transpose(src, mWidth / 2, tmpScaledBuf, mWidth / 2, regular_view_width / 2, mHeight / 2, 3);
                ivdext_horizontal_downscale_and_transpose(tmpScaledBuf, mWidth / 2, dst, mWidth / 2, regular_view_height / 2, regular_view_width / 2, 2);
                 
            } else {
                //Y
                src = (uint8_t *)as_view_disp_bufs[view_id].pv_y_buf;
                dst = outData_Y + pad_offset_row + (pad_offset_col * mWidth) + (col_start_offset * regular_view_width) + (row_start_offset * mWidth * regular_view_height);
                ivdext_horizontal_downscale_no_transpose(src, mWidth, dst, mWidth, regular_view_width, regular_view_height, numViews);

                //U
                src = (uint8_t *)as_view_disp_bufs[view_id].pv_u_buf;
                dst = outData_U + (pad_offset_row / 2) + (pad_offset_col * mWidth / 4) + (col_start_offset * regular_view_width / 2) + (row_start_offset * mWidth * regular_view_height / 4);
                ivdext_horizontal_downscale_no_transpose(src, mWidth / 2, dst, mWidth / 2, regular_view_width / 2, regular_view_height / 2, numViews);

                //V
                src = (uint8_t *)as_view_disp_bufs[view_id].pv_v_buf;
                dst = outData_V + (pad_offset_row / 2) + (pad_offset_col * mWidth / 4) + (col_start_offset * regular_view_width / 2) + (row_start_offset * mWidth * regular_view_height / 4);
                ivdext_horizontal_downscale_no_transpose(src, mWidth / 2, dst, mWidth / 2, regular_view_width / 2, regular_view_height / 2, numViews);
            }
        }

        // paint black pillers if needed
        if (pad_offset_row)
        {
            // Y
            UWORD8 *dst_start = outData_Y;
            UWORD8 *dst_end = dst_start + (num_views_in_row * regular_view_width) + pad_offset_row;

            for (int i = 0; i < mHeight; i++)
            {
                memset(dst_start, 0, pad_offset_row);
                memset(dst_end, 0, pad_offset_row);
                dst_start += mWidth;
                dst_end += mWidth;
            }

            // U
            dst_start = outData_U;
            dst_end = dst_start + (num_views_in_row * regular_view_width / 2) + (pad_offset_row / 2);

            for (int i = 0; i < mHeight / 2; i++)
            {
                memset(dst_start, 128, pad_offset_row / 2);
                memset(dst_end, 128, pad_offset_row / 2);
                dst_start += mWidth / 2;
                dst_end += mWidth / 2;
            }

            // V
            dst_start = outData_V;
            dst_end = dst_start + (num_views_in_row * regular_view_width / 2) + (pad_offset_row / 2);

            for (int i = 0; i < mHeight / 2; i++)
            {
                memset(dst_start, 128, pad_offset_row / 2);
                memset(dst_end, 128, pad_offset_row / 2);
                dst_start += mWidth / 2;
                dst_end += mWidth / 2;
            }
        }

        // Bring the views to center
        if(pad_offset_col)
        {
            // Y
            UWORD8 *dst_start = outData_Y;
            UWORD8 *dst_end = dst_start + (pad_offset_col * mWidth) +
                        (num_views_in_col * regular_view_height * mWidth);

            memset(dst_start, 0, mWidth * pad_offset_col);
            memset(dst_end, 0, mWidth * pad_offset_col);

            // U
            dst_start = outData_U;
            dst_end = dst_start + (pad_offset_col * mWidth / 4) +
                        (num_views_in_col * regular_view_height * mWidth / 4);

            memset(dst_start, 128, mWidth * pad_offset_col / 4);
            memset(dst_end, 128, mWidth * pad_offset_col / 4);

            // V
            dst_start = outData_V;
            dst_end = dst_start + (pad_offset_col * mWidth / 4) +
                        (num_views_in_col * regular_view_height * mWidth / 4);

            memset(dst_start, 128, mWidth * pad_offset_col / 4);
            memset(dst_end, 128, mWidth * pad_offset_col / 4);
        }
        free(tmpScaledBuf);            
    }
}

//#define NUM_OF_ZERO_BYTES_BEFORE_START_CODE 3

WORD32 ih264d_find_start_code(UWORD8 *pu1_buf,
                              UWORD32 u4_cur_pos,
                              UWORD32 u4_max_ofst,
                              UWORD32 *pu4_length_of_start_code) {
    WORD32 zero_byte_cnt = 0;
    UWORD32 ui_curPosTemp;
    *pu4_length_of_start_code = 0;
    /*Find first start code */
    while(u4_cur_pos < u4_max_ofst)
    {
        if(pu1_buf[u4_cur_pos] == 0)
            zero_byte_cnt++;
        else if(pu1_buf[u4_cur_pos]
                        == 0x01 && zero_byte_cnt >= NUM_OF_ZERO_BYTES_BEFORE_START_CODE)
        {
            /* Found the start code */
            u4_cur_pos++;
            break;
        }
        else
        {
            zero_byte_cnt = 0;
        }
        u4_cur_pos++;
    }
    /*Find Next Start Code */
    *pu4_length_of_start_code = u4_cur_pos;
    zero_byte_cnt = 0;
    ui_curPosTemp = u4_cur_pos;
    while(u4_cur_pos < u4_max_ofst)
    {

        if(pu1_buf[u4_cur_pos] == 0)
            zero_byte_cnt++;
        else if(pu1_buf[u4_cur_pos]
                        == 0x01 && zero_byte_cnt >= NUM_OF_ZERO_BYTES_BEFORE_START_CODE)
        {
            return (u4_cur_pos - zero_byte_cnt - ui_curPosTemp);
        }
        else
        {
            zero_byte_cnt = 0;
        }
        u4_cur_pos++;
    }

    return (u4_cur_pos - zero_byte_cnt - ui_curPosTemp); //(START_CODE_NOT_FOUND);
}

// TODO: can overall error checking be improved?
// TODO: allow configuration of color format and usage for graphic buffers instead
//       of hard coding them to HAL_PIXEL_FORMAT_YV12
// TODO: pass coloraspects information to surface
// TODO: test support for dynamic change in resolution
// TODO: verify if the decoder sent back all frames
void C2SoftAvcDec::process(
        const std::unique_ptr<C2Work> &work,
        const std::shared_ptr<C2BlockPool> &pool) {
    UNUSED(kMinInputBytes);
    uint8_t *mStrBufTemp;
    mStrBufTemp = (uint8_t *)ivd_aligned_malloc(nullptr, 128, 8000000);

    // Initialize output work
    work->result = C2_OK;
    work->workletsProcessed = 0u;
    work->worklets.front()->output.flags = work->input.flags;
    if (mSignalledError || mSignalledOutputEos) {
        work->result = C2_BAD_VALUE;
        return;
    }

    size_t inOffset = 0u;
    size_t inSize = 0u;
    uint32_t workIndex = work->input.ordinal.frameIndex.peeku() & 0xFFFFFFFF;
    C2ReadView rView = mDummyReadView;
    if (!work->input.buffers.empty()) {
        rView = work->input.buffers[0]->data().linearBlocks().front().map().get();
        inSize = rView.capacity();
        if (inSize && rView.error()) {
            ALOGE("read view map failed %d", rView.error());
            work->result = rView.error();
            return;
        }
    }
    bool eos = ((work->input.flags & C2FrameData::FLAG_END_OF_STREAM) != 0);

    bool hasPicture = false;

    ALOGV("in buffer attr. size %zu timestamp %lu frameindex %d, flags %x",
          inSize, (long unsigned int) work->input.ordinal.timestamp.peeku(),
          (int)work->input.ordinal.frameIndex.peeku(), work->input.flags);
    size_t inPos = 0;
    uint32_t i;

    memcpy(mStrBufTemp, const_cast<uint8_t *>(rView.data()), inSize);

    //PREPARING OR STORING INPUT [ nal units]
    while (inPos < inSize){
        if (C2_OK != ensureDecoderState(pool)) {
            mSignalledError = true;
            work->workletsProcessed = 1u;
            work->result = C2_CORRUPTED;
            return;
        }
        frameNum++;
        if(frameNum == 2) {
            secondFrame = true;
        }

		imvcd_video_decode_ip_t s_decode_ip;
        imvcd_video_decode_op_t s_decode_op;

        imvcd_video_decode_ip_t *ps_decode_ip = &s_decode_ip;
        imvcd_video_decode_op_t *ps_decode_op = &s_decode_op;
         
        C2GraphicView wView = mOutBlock->map().get();
        if (wView.error()) {
            ALOGE("graphic view map failed %d", wView.error());
            work->result = wView.error();
            return;
        }

        for(i = 0; i < mDispBufProps.s_mvc_buf_info.u2_num_views; i++) {
            mOutBuf[i] = (uint8_t *)ivd_aligned_malloc(nullptr, 128, mStride * mHeight * 2);
            if (!mOutBuf[i]) {
                ALOGE("could not allocate tmp output buffer %u of size %u ", i, mStride * mHeight * 2);
                return;
            }
        }
        
        WORD32 err_code;

        {  
            if (!setDecodeArgs(ps_decode_ip, ps_decode_op, &s_out_buf, &rView, &wView,
                               inOffset + inPos, inSize - inPos, workIndex)) {
                mSignalledError = true;
                work->workletsProcessed = 1u;
                work->result = C2_CORRUPTED;
                return;
            }
            ps_decode_op->ps_view_disp_bufs = as_view_disp_bufs;
            
            if (false == mHeaderDecoded) {
                /* Decode header and get dimensions */
                setParams(mStride, IVD_DECODE_HEADER);
            }
            if (work->input.flags == (uint32_t)0) {
                setParams(mStride, IVD_DECODE_FRAME);
            }
            else {
                setParams(mStride, IVD_DECODE_HEADER);
            }
            WORD32 delay;
            GETTIME(&mTimeStart, nullptr);
            TIME_DIFF(mTimeEnd, mTimeStart, delay);
            err_code = (WORD32)ivdec_api_function(mDecHandle, ps_decode_ip, ps_decode_op);    
            WORD32 decodeTime;
            GETTIME(&mTimeEnd, nullptr);
            TIME_DIFF(mTimeStart, mTimeEnd, decodeTime);
            ALOGV("decodeTime=%6d delay=%6d numBytes=%6d errorCode=%x, err_code = %x", decodeTime, delay,
                  ps_decode_op->s_ivd_op.u4_num_bytes_consumed, ps_decode_op->s_ivd_op.u4_error_code,err_code);
        }
        if (IVD_MEM_ALLOC_FAILED == (ps_decode_op->s_ivd_op.u4_error_code & IVD_ERROR_MASK)) {
            ALOGE("allocation failure in decoder");
            mSignalledError = true;
            work->workletsProcessed = 1u;
            work->result = C2_CORRUPTED;
            return;
        } else if (IVD_STREAM_WIDTH_HEIGHT_NOT_SUPPORTED ==
                (ps_decode_op->s_ivd_op.u4_error_code & IVD_ERROR_MASK)) {
            ALOGE("unsupported resolution : %dx%d", mWidth, mHeight);
            mSignalledError = true;
            work->workletsProcessed = 1u;
            work->result = C2_CORRUPTED;
            return;
        } else if (IVD_RES_CHANGED == (ps_decode_op->s_ivd_op.u4_error_code & IVD_ERROR_MASK)) {
            ALOGV("resolution changed");
            drainInternal(DRAIN_COMPONENT_NO_EOS, pool, work);
            resetDecoder();
            resetPlugin();
            work->workletsProcessed = 0u;

            (void)  ivdec_api_function(mDecHandle, ps_decode_ip, ps_decode_op);
            getBufInfo(&wView);

            mWidth = ps_decode_op->s_ivd_op.u4_pic_wd;
            mHeight = ps_decode_op->s_ivd_op.u4_pic_ht;
            mStride = ALIGN128(ps_decode_op->s_ivd_op.u4_pic_wd);

            size_t lumaSize = mStride * mHeight;
            size_t chromaSize = lumaSize >> 2;

            for(i = 0; i < mDispBufProps.s_mvc_buf_info.u2_num_views; i++) {
                mOutBuf[i] = (uint8_t *)ivd_aligned_malloc(nullptr, 128, mStride * mHeight * 2);
                if (!mOutBuf[i]) {
                    ALOGE("could not allocate tmp output buffer %u of size %u ", i, mStride * mHeight * 2);
                    return;
                }
            }

            for(i = 0; i < mDispBufProps.s_mvc_buf_info.u2_num_views; i++) {   
                ps_decode_ip->s_ivd_ip.s_out_buffer.pu1_bufs[0] = mOutBuf[i];
                // ps_decode_ip->s_out_buffer.ps_view_buf_props[i].as_component_bufs[0].i4_data_stride = mStride;

                ps_decode_ip->s_ivd_ip.s_out_buffer.pu1_bufs[1] = mOutBuf[i] + lumaSize;
                // ps_decode_ip->s_out_buffer.ps_view_buf_props[i].as_component_bufs[1].i4_data_stride = mStride >> 1;

                ps_decode_ip->s_ivd_ip.s_out_buffer.pu1_bufs[2] = mOutBuf[i] + lumaSize + chromaSize;
                // ps_decode_ip->s_out_buffer.ps_view_buf_props[i].as_component_bufs[2].i4_data_stride = mStride >> 1;

                // ps_decode_ip->s_out_buffer.ps_view_buf_props[i].u2_width = mWidth;
                // ps_decode_ip->s_out_buffer.ps_view_buf_props[i].u2_height = mHeight;
                // ps_decode_ip->s_out_buffer.ps_view_buf_props[i].u1_bit_depth = 8;
                
            }

            setParams(mStride, IVD_DECODE_FRAME);
            (void) ivdec_api_function(mDecHandle, ps_decode_ip, ps_decode_op);
        } else if (IS_IVD_FATAL_ERROR(ps_decode_op->s_ivd_op.u4_error_code)) {
            ALOGE("Fatal error in decoder 0x%x", ps_decode_op->s_ivd_op.u4_error_code);
            mSignalledError = true;
            work->workletsProcessed = 1u;
            work->result = C2_CORRUPTED;
            return;
        }

        if (ps_decode_op->s_ivd_op.i4_reorder_depth >= 0 && mOutputDelay != ps_decode_op->s_ivd_op.i4_reorder_depth) {
            mOutputDelay = ps_decode_op->s_ivd_op.i4_reorder_depth;

            C2PortActualDelayTuning::output outputDelay(mOutputDelay);
            std::vector<std::unique_ptr<C2SettingResult>> failures;
            c2_status_t err =
                mIntf->config({&outputDelay}, C2_MAY_BLOCK, &failures);
            if (err == OK) {
                work->worklets.front()->output.configUpdate.push_back(
                    C2Param::Copy(outputDelay));
            } else {
                ALOGE("Cannot set output delay");
                mSignalledError = true;
                work->workletsProcessed = 1u;
                work->result = C2_CORRUPTED;
                return;
            }
        }
        if (0 < ps_decode_op->s_ivd_op.u4_pic_wd && 0 < ps_decode_op->s_ivd_op.u4_pic_ht) {
            if (mHeaderDecoded == false && secondFrame == true) {
                mHeaderDecoded = true;
                mIsHeaderReady = false;
                mStride = ALIGN128(ps_decode_op->s_ivd_op.u4_pic_wd);
                /* Header is Decoded. Call getBufInfo */
                getBufInfo(&wView);
                setParams(mStride, IVD_DECODE_FRAME);
            }
            if (ps_decode_op->s_ivd_op.u4_pic_wd != mWidth || ps_decode_op->s_ivd_op.u4_pic_ht != mHeight) {
                mWidth = ps_decode_op->s_ivd_op.u4_pic_wd;
                mHeight = ps_decode_op->s_ivd_op.u4_pic_ht;
                mStride = ALIGN128(ps_decode_op->s_ivd_op.u4_pic_wd);
                getBufInfo(&wView);
                CHECK_EQ(0u, ps_decode_op->s_ivd_op.u4_output_present);
                
                C2StreamPictureSizeInfo::output size(0u, mStride, mHeight);
                std::vector<std::unique_ptr<C2SettingResult>> failures;
                c2_status_t err = mIntf->config({&size}, C2_MAY_BLOCK, &failures);
                if (err == OK) {
                    work->worklets.front()->output.configUpdate.push_back(
                        C2Param::Copy(size));
                } else {
                    ALOGE("Cannot set width and height");
                    mSignalledError = true;
                    work->workletsProcessed = 1u;
                    work->result = C2_CORRUPTED;
                    return;
                }
                //continue;
            } 
        }
        
        (void)getVuiParams();
        hasPicture |= (1 == ps_decode_op->s_ivd_op.u4_frame_decoded_flag);
        if (ps_decode_op->s_ivd_op.u4_output_present) {
            stitchViews(&wView);
            finishWork(ps_decode_op->s_ivd_op.u4_ts, work);
        }

        //free bufs
        for(i = 0; i < mDispBufProps.s_mvc_buf_info.u2_num_views; i++) {
             if (mOutBuf[i]) {
                ivd_aligned_free(nullptr, mOutBuf[i]);
                mOutBuf[i] = nullptr;
            }
        }

        inPos += ps_decode_op->s_ivd_op.u4_num_bytes_consumed;
        break;
    }

    if (eos) {
        drainInternal(DRAIN_COMPONENT_WITH_EOS, pool, work);
        mSignalledOutputEos = true;
        frameNum = 0;
        secondFrame = false;
        mHeaderDecoded = false;
    } else if (!hasPicture) {
        fillEmptyWork(work);
    }

    work->input.buffers.clear();
    ivd_aligned_free(nullptr, mStrBufTemp);
}

c2_status_t C2SoftAvcDec::drainInternal(
        uint32_t drainMode,
        const std::shared_ptr<C2BlockPool> &pool,
        const std::unique_ptr<C2Work> &work) {
    if (drainMode == NO_DRAIN) {
        ALOGW("drain with NO_DRAIN: no-op");
        return C2_OK;
    }
    if (drainMode == DRAIN_CHAIN) {
        ALOGW("DRAIN_CHAIN not supported");
        return C2_OMITTED;
    }

    if (OK != setFlushMode()) return C2_CORRUPTED;
    uint32_t i;
    while (true) {
        if (C2_OK != ensureDecoderState(pool)) {
            mSignalledError = true;
            work->workletsProcessed = 1u;
            work->result = C2_CORRUPTED;
            return C2_CORRUPTED;
        }
        C2GraphicView wView = mOutBlock->map().get(); 
        if (wView.error()) {
            ALOGE("graphic view map failed %d", wView.error());
            return C2_CORRUPTED;
        }

        for(i = 0; i < mDispBufProps.s_mvc_buf_info.u2_num_views; i++) {
            mOutBuf[i] = (uint8_t *)ivd_aligned_malloc(nullptr, 128, mStride * mHeight * 2);
            if (!mOutBuf[i]) {
                ALOGE("could not allocate tmp output buffer %u of size %u ", i, mStride * mHeight * 2);
                return C2_NO_MEMORY;
            }
        }

        imvcd_video_decode_ip_t s_decode_ip;
        imvcd_video_decode_op_t s_decode_op;

        imvcd_video_decode_ip_t *ps_decode_ip = &s_decode_ip;
        imvcd_video_decode_op_t *ps_decode_op = &s_decode_op;

        if (!setDecodeArgs(ps_decode_ip, ps_decode_op, &s_out_buf, nullptr, &wView, 0, 0, 0)) {
            mSignalledError = true;
            work->workletsProcessed = 1u;
            return C2_CORRUPTED;
        }
        (void) ivdec_api_function(mDecHandle, ps_decode_ip, ps_decode_op);

        if (ps_decode_op->s_ivd_op.u4_output_present) {
            stitchViews(&wView);
            finishWork(ps_decode_op->s_ivd_op.u4_ts, work);
        } else {
            fillEmptyWork(work);
            break;
        }
        
        //free bufs
        for(i = 0; i < mDispBufProps.s_mvc_buf_info.u2_num_views; i++) {
            if (mOutBuf[i]) {
                ivd_aligned_free(nullptr, mOutBuf[i]);
                mOutBuf[i] = nullptr;
            }
        }
    }

    return C2_OK;
}

c2_status_t C2SoftAvcDec::drain(
        uint32_t drainMode,
        const std::shared_ptr<C2BlockPool> &pool) {
    return drainInternal(drainMode, pool, nullptr);
}

class C2SoftAvcDecFactory : public C2ComponentFactory {
public:
    C2SoftAvcDecFactory() : mHelper(std::static_pointer_cast<C2ReflectorHelper>(
        GetCodec2PlatformComponentStore()->getParamReflector())) {
    }

    virtual c2_status_t createComponent(
            c2_node_id_t id,
            std::shared_ptr<C2Component>* const component,
            std::function<void(C2Component*)> deleter) override {
        *component = std::shared_ptr<C2Component>(
                new C2SoftAvcDec(COMPONENT_NAME,
                                 id,
                                 std::make_shared<C2SoftAvcDec::IntfImpl>(mHelper)),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id,
            std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new SimpleInterface<C2SoftAvcDec::IntfImpl>(
                        COMPONENT_NAME, id, std::make_shared<C2SoftAvcDec::IntfImpl>(mHelper)),
                deleter);
        return C2_OK;
    }

    virtual ~C2SoftAvcDecFactory() override = default;

private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
};

}  // namespace android

__attribute__((cfi_canonical_jump_table))
extern "C" ::C2ComponentFactory* CreateCodec2Factory() {
    ALOGV("in %s", __func__);
    return new ::android::C2SoftAvcDecFactory();
}

__attribute__((cfi_canonical_jump_table))
extern "C" void DestroyCodec2Factory(::C2ComponentFactory* factory) {
    ALOGV("in %s", __func__);
    delete factory;
}
