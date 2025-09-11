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

#ifndef ANDROID_C2_SOFT_MVC_DEC_H_
#define ANDROID_C2_SOFT_MVC_DEC_H_

#define FILE_DUMP_ENABLE
#include <sys/time.h>
#include <cassert>
#include <queue>
#include <media/stagefright/foundation/ColorUtils.h>

#include <atomic>
#include <SimpleC2Component.h>

#include "ih264_typedefs.h"
#include "ih264d.h"
#include "imvcd.h"
#include "iv.h"
#include "ivd.h"
#include "imvcd.h"
#include "imvc_structs.h"
#include "ih264_downscaler.h"

namespace android {

#define ivdec_api_function                                          imvcd_api_function
#define ivdext_horizontal_downscale_no_transpose                    ih264e_horizontal_downscale_no_transpose
#define ivdext_horizontal_downscale_and_transpose                   ih264e_horizontal_downscale_and_transpose

#define ivdext_create_ip_t              ih264d_create_ip_t
#define ivdext_create_op_t              ih264d_create_op_t
#define ivdext_delete_ip_t              ih264d_delete_ip_t
#define ivdext_delete_op_t              ih264d_delete_op_t
#define ivdext_ctl_set_num_cores_ip_t   ih264d_ctl_set_num_cores_ip_t
#define ivdext_ctl_set_num_cores_op_t   ih264d_ctl_set_num_cores_op_t
#define ivdext_ctl_get_vui_params_ip_t  ih264d_ctl_get_vui_params_ip_t
#define ivdext_ctl_get_vui_params_op_t  ih264d_ctl_get_vui_params_op_t
#define ALIGN32(x)                      ((((x) + 31) >> 5) << 5)
#define ALIGN128(x)                     ((((x) + 127) >> 7) << 7)
#define MAX_NUM_CORES                   4
#define MAX_NUM_VIEWS                   6
#define IVDEXT_CMD_CTL_SET_NUM_CORES    \
        (IVD_CONTROL_API_COMMAND_TYPE_T)IH264D_CMD_CTL_SET_NUM_CORES
#define MIN(a, b)                       (((a) < (b)) ? (a) : (b))
#define GETTIME(a, b)                   gettimeofday(a, b);
#define TIME_DIFF(start, end, diff)     \
    diff = (((end).tv_sec - (start).tv_sec) * 1000000) + \
            ((end).tv_usec - (start).tv_usec);

#ifdef FILE_DUMP_ENABLE
    #define INPUT_DUMP_PATH     "/sdcard/clips/avcd_input"
    #define INPUT_DUMP_EXT      "h264"
    #define GENERATE_FILE_NAMES() {                         \
        GETTIME(&mTimeStart, NULL);                         \
        strcpy(mInFile, "");                                \
        sprintf(mInFile, "%s_%ld.%ld.%s", INPUT_DUMP_PATH,  \
                mTimeStart.tv_sec, mTimeStart.tv_usec,      \
                INPUT_DUMP_EXT);                            \
    }
    #define CREATE_DUMP_FILE(m_filename) {                  \
        FILE *fp = fopen(m_filename, "wb");                 \
        if (fp != NULL) {                                   \
            fclose(fp);                                     \
        } else {                                            \
            ALOGD("Could not open file %s", m_filename);    \
        }                                                   \
    }
    #define DUMP_TO_FILE(m_filename, m_buf, m_size, m_offset)\
    {                                                       \
        FILE *fp = fopen(m_filename, "ab");                 \
        if (fp != NULL && m_buf != NULL && m_offset == 0) { \
            int i;                                          \
            i = fwrite(m_buf, 1, m_size, fp);               \
            ALOGD("fwrite ret %d to write %d", i, m_size);  \
            if (i != (int) m_size) {                        \
                ALOGD("Error in fwrite, returned %d", i);   \
                perror("Error in write to file");           \
            }                                               \
        } else if (fp == NULL) {                            \
            ALOGD("Could not write to file %s", m_filename);\
        }                                                   \
        if (fp) {                                           \
            fclose(fp);                                     \
        }                                                   \
    }
#else /* FILE_DUMP_ENABLE */
    #define INPUT_DUMP_PATH
    #define INPUT_DUMP_EXT
    #define OUTPUT_DUMP_PATH
    #define OUTPUT_DUMP_EXT
    #define GENERATE_FILE_NAMES()
    #define CREATE_DUMP_FILE(m_filename)
    #define DUMP_TO_FILE(m_filename, m_buf, m_size, m_offset)
#endif /* FILE_DUMP_ENABLE */

struct Nalu_Data {
    std::vector<UWORD8> bitstrm;
    uint32_t inp_ts;
};

class C2SoftAvcDec : public SimpleC2Component {
public:
    class IntfImpl;
    C2SoftAvcDec(const char *name, c2_node_id_t id, const std::shared_ptr<IntfImpl> &intfImpl);
    virtual ~C2SoftAvcDec();

    // From SimpleC2Component
    c2_status_t onInit() override;
    c2_status_t onStop() override;
    void onReset() override;
    void onRelease() override;
    c2_status_t onFlush_sm() override;
    void process(
            const std::unique_ptr<C2Work> &work,
            const std::shared_ptr<C2BlockPool> &pool) override;
    c2_status_t drain(
            uint32_t drainMode,
            const std::shared_ptr<C2BlockPool> &pool) override;

private:
    status_t createDecoder();
    status_t setNumCores();
    status_t setParams(size_t stride, IVD_VIDEO_DECODE_MODE_T dec_mode);
    status_t outBufAlloc(ivd_out_bufdesc_t *ps_out_buf, C2GraphicView *wView);
    status_t getBufInfo(C2GraphicView *wView);
    status_t initDecoder();
    status_t resetState();
    bool setDecodeArgs(imvcd_video_decode_ip_t *ps_decode_ip,
                       imvcd_video_decode_op_t *ps_decode_op,
                       ivd_out_bufdesc_t *ps_out_buf,
                       C2ReadView *inBuffer,
                       C2GraphicView *outBuffer,
                       size_t inOffset,
                       size_t inSize,
                       uint32_t tsMarker);
    bool getVuiParams();
    c2_status_t ensureDecoderState(const std::shared_ptr<C2BlockPool> &pool);
    void finishWork(uint64_t index, const std::unique_ptr<C2Work> &work);
    void stitchViews(C2GraphicView *wView);
    void stitchViewsNew(C2GraphicView *wView);
    status_t setFlushMode();
    c2_status_t drainInternal(
            uint32_t drainMode,
            const std::shared_ptr<C2BlockPool> &pool,
            const std::unique_ptr<C2Work> &work);
    status_t resetDecoder();
    void resetPlugin();
    status_t deleteDecoder();

    std::shared_ptr<IntfImpl> mIntf;

    // TODO:This is not the right place for this enum. These should
    // be part of c2-vndk so that they can be accessed by all video plugins
    // until then, make them feel at home
    enum {
        kNotSupported,
        kPreferBitstream,
        kPreferContainer,
    };

    typedef enum AVC_EXT_NALU_ID_T {
        UNSPEC_0 = 0,

        SLICE_NON_IDR = 1,

        SLICE_DPA = 2,

        SLICE_DPB = 3,

        SLICE_DPC = 4,

        SLICE_IDR = 5,

        SEI = 6,

        SPS = 7,

        PPS = 8,

        AUD = 9,

        EOSEQ = 10,

        EOSTR = 11,

        FILLER = 12,

        SPSE = 13,

        PREFIX_NAL = 14,

        SUBSET_SPS = 15,

        AUX_PIC = 19,

        CODED_SLICE_EXTENSION = 20,

        UNSPEC_31 = 24

    } AVC_EXT_NALU_ID_T;

    iv_obj_t *mDecHandle;
    std::shared_ptr<C2GraphicBlock> mOutBlock;
    uint8_t *mOutBufferFlush[MAX_NUM_VIEWS];
    uint8_t *mOutBuf[MAX_NUM_VIEWS];
    iv_yuv_buf_t as_view_disp_bufs[MAX_NUM_VIEWS];
    imvcd_get_buf_info_op_t mDispBufProps;
    ivd_out_bufdesc_t s_out_buf;

    //0 -> SPS
    //1 -> SUb sps
    //2 -> pps
    //3 -> nal prefix
    //4 -> slice 
    std::queue<Nalu_Data> nalu_q[6];
    bool mIsHeaderReady;
    bool mIsSliceReady;
    bool mIsFlushMode;
    
    std::vector<uint8_t> bitstream;
    size_t mNumCores;
    IV_COLOR_FORMAT_T mIvColorFormat;
    uint32_t mOutputDelay;
    uint32_t frameNum;
    bool secondFrame;
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mStride;
    bool mSignalledOutputEos;
    bool mSignalledError;
    bool mHeaderDecoded;
    std::atomic_uint64_t mOutIndex;
    // Color aspects. These are ISO values and are meant to detect changes in aspects to avoid
    // converting them to C2 values for each frame
    struct VuiColorAspects {
        uint8_t primaries;
        uint8_t transfer;
        uint8_t coeffs;
        uint8_t fullRange;

        // default color aspects
        VuiColorAspects()
            : primaries(2), transfer(2), coeffs(2), fullRange(0) { }

        bool operator==(const VuiColorAspects &o) const {
            return primaries == o.primaries && transfer == o.transfer && coeffs == o.coeffs
                    && fullRange == o.fullRange;
        }
    } mBitstreamColorAspects;

    // profile
    struct timeval mTimeStart;
    struct timeval mTimeEnd;
#ifdef FILE_DUMP_ENABLE
    char mInFile[200];
#endif /* FILE_DUMP_ENABLE */

    C2_DO_NOT_COPY(C2SoftAvcDec);
};


}  // namespace android

#endif  // ANDROID_C2_SOFT_AVC_DEC_H_
