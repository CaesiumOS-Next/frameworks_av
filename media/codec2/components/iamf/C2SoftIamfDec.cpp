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

#define LOG_TAG "C2SoftIamfDec"
#include <log/log.h>

#include <C2PlatformSupport.h>
#include <SimpleC2Interface.h>
#include "C2SoftIamfDec.h"
#include "LayoutTranslation.h"
#include "iamf/api/types.h"

namespace android {

namespace {

constexpr char COMPONENT_NAME[] = "c2.android.iamf.decoder";

}  // namespace

using ::iamf_tools::api::IamfDecoder;

class C2SoftIamfDec::IntfImpl : public SimpleInterface<void>::BaseParams {
  public:
    explicit IntfImpl(const std::shared_ptr<C2ReflectorHelper>& helper)
        : SimpleInterface<void>::BaseParams(
                  helper, COMPONENT_NAME, C2Component::KIND_DECODER, C2Component::DOMAIN_AUDIO,
                  // Replace with MEDIA_MIMETYPE_AUDIO_IAMF when available in MediaDefs.h
                  "audio/iamf") {
        noInputLatency();
        noPrivateBuffers();
        noInputReferences();
        noOutputReferences();
        noTimeStretch();
        setDerivedInstance(this);

        // Not sure if this is necessary or correct, but it seems to be in all the audio decoders.
        // DO NOT SUBMIT
        addParameter(DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
                             .withConstValue(new C2ComponentAttributesSetting(
                                     C2Component::ATTRIB_IS_TEMPORAL))
                             .build());

        // Parameters for the input bitstream
        addParameter(
                DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                        .withDefault(new C2StreamProfileLevelInfo::input(
                                0u, C2Config::PROFILE_IAMF_SIMPLE_PCM, C2Config::LEVEL_UNUSED))
                        .withFields({C2F(mProfileLevel, profile)
                                             .oneOf({
                                                     C2Config::PROFILE_IAMF_SIMPLE_AAC,
                                                     C2Config::PROFILE_IAMF_SIMPLE_FLAC,
                                                     C2Config::PROFILE_IAMF_SIMPLE_OPUS,
                                                     C2Config::PROFILE_IAMF_SIMPLE_PCM,
                                                     C2Config::PROFILE_IAMF_BASE_AAC,
                                                     C2Config::PROFILE_IAMF_BASE_FLAC,
                                                     C2Config::PROFILE_IAMF_BASE_OPUS,
                                                     C2Config::PROFILE_IAMF_BASE_PCM,
                                                     C2Config::PROFILE_IAMF_BASE_ENHANCED_AAC,
                                                     C2Config::PROFILE_IAMF_BASE_ENHANCED_FLAC,
                                                     C2Config::PROFILE_IAMF_BASE_ENHANCED_OPUS,
                                                     C2Config::PROFILE_IAMF_BASE_ENHANCED_PCM,
                                             }),
                                     C2F(mProfileLevel, level).oneOf({C2Config::LEVEL_UNUSED})})
                        .withSetter(ProfileLevelSetter)
                        .build());

        addParameter(DefineParam(mInputMaxBufSize, C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE)
                             .withConstValue(new C2StreamMaxBufferSizeInfo::input(
                                     0u, 32768))  // TODO: Why any given size?
                                                  // Opus: 960*6, Aac: 8196, Flac: 32768
                             .build());

        // Params for the output audio
        addParameter(
                DefineParam(mSampleRate, C2_PARAMKEY_SAMPLE_RATE)
                        .withDefault(new C2StreamSampleRateInfo::output(0u, 48000))
                        // IAMF allows PCM with sample rates {44.1k, 16k, 32k, 48k, 96k}
                        // Opus is always 48k?
                        .withFields(
                                {C2F(mSampleRate, value).oneOf(16000, 32000, 44100, 48000, 96000)})
                        .withSetter((Setter<decltype(*mSampleRate)>::StrictValueWithNoDeps))
                        .build());

        addParameter(
                DefineParam(mRenderedChannelMask, C2_PARAMKEY_CHANNEL_MASK)
                        .withDefault(new C2StreamChannelMaskInfo::output(0u, 0))
                        .withFields({C2F(mRenderedChannelMask, value).inRange(0, 4294967292)})
                        .withSetter(Setter<decltype(*mRenderedChannelMask)>::StrictValueWithNoDeps)
                        .build());

        addParameter(
                DefineParam(mPcmEncodingInfo, C2_PARAMKEY_PCM_ENCODING)
                        .withDefault(new C2StreamPcmEncodingInfo::output(0u, C2Config::PCM_16))
                        .withFields({C2F(mPcmEncodingInfo, value)
                                             .oneOf({C2Config::PCM_16,
                                                     // C2Config::PCM_8,
                                                     C2Config::PCM_FLOAT})})
                        .withSetter((Setter<decltype(*mPcmEncodingInfo)>::StrictValueWithNoDeps))
                        .build());
    }

    uint32_t getOutputChannelMask() const { return mRenderedChannelMask->value; }

  private:
    // Opus: C2_PARAMKEY_SAMPLE_RATE, C2_PARAMKEY_CHANNEL_COUNT, C2_PARAMKEY_BITRATE,
    //       C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE
    // AAC: C2_PARAMKEY_OUTPUT_DELAY, C2_PARAMKEY_SAMPLE_RATE, C2_PARAMKEY_CHANNEL_COUNT,
    //      C2_PARAMKEY_MAX_CHANNEL_COUNT, C2_PARAMKEY_BITRATE,
    //      C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE, C2_PARAMKEY_AAC_PACKAGING,
    //      C2_PARAMKEY_PROFILE_LEVEL, C2_PARAMKEY_DRC_COMPRESSION_MODE,
    //      C2_PARAMKEY_DRC_TARGET_REFERENCE_LEVEL, C2_PARAMKEY_DRC_ENCODED_TARGET_LEVEL,
    //      C2_PARAMKEY_DRC_BOOST_FACTOR C2_PARAMKEY_DRC_ATTENUATION_FACTOR,
    //      C2_PARAMKEY_DRC_EFFECT_TYPE, C2_PARAMKEY_DRC_ALBUM_MODE
    //      C2_PARAMKEY_DRC_OUTPUT_LOUDNESS, C2_PARAMKEY_CHANNEL_MASK
    // FLAC:C2_PARAMKEY_SAMPLE_RATE, C2_PARAMKEY_CHANNEL_COUNT, C2_PARAMKEY_BITRATE,
    // C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE
    //      C2_PARAMKEY_PCM_ENCODING
    // Opus
    // std::shared_ptr<C2StreamSampleRateInfo::output> mSampleRate;
    // std::shared_ptr<C2StreamChannelCountInfo::output> mChannelCount;
    // std::shared_ptr<C2StreamBitrateInfo::input> mBitrate;
    // std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mInputMaxBufSize;
    // AAC
    // std::shared_ptr<C2StreamSampleRateInfo::output> mSampleRate;
    // std::shared_ptr<C2StreamChannelCountInfo::output> mChannelCount;
    // std::shared_ptr<C2StreamBitrateInfo::input> mBitrate;
    // std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mInputMaxBufSize;
    // std::shared_ptr<C2StreamAacFormatInfo::input> mAacFormat;
    // std::shared_ptr<C2StreamProfileLevelInfo::input> mProfileLevel;
    // std::shared_ptr<C2StreamDrcCompressionModeTuning::input> mDrcCompressMode;
    // std::shared_ptr<C2StreamDrcTargetReferenceLevelTuning::input> mDrcTargetRefLevel;
    // std::shared_ptr<C2StreamDrcEncodedTargetLevelTuning::input> mDrcEncTargetLevel;
    // std::shared_ptr<C2StreamDrcBoostFactorTuning::input> mDrcBoostFactor;
    // std::shared_ptr<C2StreamDrcAttenuationFactorTuning::input> mDrcAttenuationFactor;
    // std::shared_ptr<C2StreamDrcEffectTypeTuning::input> mDrcEffectType;
    // std::shared_ptr<C2StreamDrcAlbumModeTuning::input> mDrcAlbumMode;
    // std::shared_ptr<C2StreamMaxChannelCountInfo::input> mMaxChannelCount;
    // std::shared_ptr<C2StreamDrcOutputLoudnessTuning::output> mDrcOutputLoudness;
    // std::shared_ptr<C2StreamChannelMaskInfo::output> mChannelMask;
    // FLAC
    // std::shared_ptr<C2StreamSampleRateInfo::output> mSampleRate;
    // std::shared_ptr<C2StreamChannelCountInfo::output> mChannelCount;
    // std::shared_ptr<C2StreamBitrateInfo::input> mBitrate;
    // std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mInputMaxBufSize;
    // std::shared_ptr<C2StreamPcmEncodingInfo::output> mPcmEncodingInfo;

    // Input
    std::shared_ptr<C2StreamProfileLevelInfo::input> mProfileLevel;
    std::shared_ptr<C2StreamMaxBufferSizeInfo::input> mInputMaxBufSize;
    // Output
    std::shared_ptr<C2StreamChannelMaskInfo::output> mRenderedChannelMask;
    std::shared_ptr<C2StreamSampleRateInfo::output> mSampleRate;
    std::shared_ptr<C2StreamPcmEncodingInfo::output> mPcmEncodingInfo;
};

C2SoftIamfDec::C2SoftIamfDec(const char* name, c2_node_id_t id,
                             const std::shared_ptr<IntfImpl>& intfImpl)
    : SimpleC2Component(std::make_shared<SimpleInterface<IntfImpl>>(name, id, intfImpl)),
      mIntf(intfImpl),
      mIamfDecoder(nullptr) {}

C2SoftIamfDec::~C2SoftIamfDec() {
    onRelease();
}

void C2SoftIamfDec::resetState() {
    mCodecDelay = 0;
    mSeekPreRoll = 0;
    mSamplesToDiscard = 0;
    mInputBufferCount = 0;
    mSignalledError = false;
    mSignalledOutputEos = false;
}

c2_status_t C2SoftIamfDec::onInit() {
    // TODO: Move target_layout inside of the Create when it is submitted.
    absl::StatusOr<IamfDecoder> decoder = IamfDecoder::Create();
    if (!decoder.ok()) {
        // IamfDecoder::Create fails if it cannot create the read bit buffer.
        return C2_NO_MEMORY;
    }
    iamf_tools::api::OutputLayout target_layout =
            c2_soft_iamf_internal::GetIamfLayout(mIntf->getOutputChannelMask());
    if (!mIamfDecoder->ConfigureOutputLayout(target_layout).ok()) {
        // Perhaps could fail if the layout is unimplemented.
        return C2_OMITTED;
    }
    resetState();
    mIamfDecoder = std::make_unique<IamfDecoder>(*std::move(decoder));
    return C2_OK;
}

c2_status_t C2SoftIamfDec::onStop() {
    if (mIamfDecoder) {
        (void)mIamfDecoder->Close();  // ?
        mIamfDecoder = nullptr;
    }
    resetState();
    return C2_OK;
}

void C2SoftIamfDec::onReset() {
    /* Resets the component.
     *
     * This method MUST be supported in all (including tripped) states other than released.
     *
     * This method MUST be supported during any other blocking call.
     *
     * This method MUST return withing 500ms.
     *
     * After this call returns all work SHALL be abandoned, all buffer references SHALL be released.
     * If the return value is C2_BAD_STATE or C2_DUPLICATE, no state change is expected as a
     * response to this call.
     * For all other return values, the component shall be in the stopped state.*/
    (void)onStop();
}

void C2SoftIamfDec::onRelease() {
    /**
     * Releases the component.
     *
     * This method MUST be supported in stopped state.
     *
     * This method MUST return withing 500ms. Upon return all references shall be abandoned.
     *
     * \retval C2_OK        the component has been released
     * \retval C2_DUPLICATE the component is already released
     * \retval C2_BAD_STATE the component is running
     * \retval C2_TIMED_OUT the component could not be released within the time limit (unexpected)
     * \retval C2_CORRUPTED some unknown error prevented releasing the component (unexpected)
     */
    if (mIamfDecoder) {
        (void)mIamfDecoder->Close();  // ?
        mIamfDecoder = nullptr;
    }
}

c2_status_t C2SoftIamfDec::onFlush_sm() {
    // Discards and abandons any pending work for the component, and optionally any component
    // downstream.
    // C2_OK        the component has been successfully flushed
    // C2_BAD_STATE when called in the stopped or released state (user error)
    // C2_TIMED_OUT the flush could not be completed within the time limit (unexpected)
    // C2_CORRUPTED some unknown error prevented flushing from completion (unexpected)
    // TODO: Do I need to worry about checking the state and returning the appropriate error
    // or does SimpleC2Component handle some or all of that?
    return C2_OK;
}

namespace {
bool GetFlagValue(C2FrameData::flags_t flags, C2FrameData::flags_t flag_to_check) {
    return flags & flag_to_check;
}

void fillEmptyWork(const std::unique_ptr<C2Work>& work) {
    work->worklets.front()->output.flags = work->input.flags;
    work->worklets.front()->output.buffers.clear();
    work->worklets.front()->output.ordinal = work->input.ordinal;
    work->workletsProcessed = 1u;
}
}  // namespace

void C2SoftIamfDec::process(const std::unique_ptr<C2Work>& work,
                            const std::shared_ptr<C2BlockPool>& pool) {
    (void)pool;
    // Assume OK.
    work->result = C2_OK;
    // Start processing first worklet.
    work->workletsProcessed = 1u;
    // For output buffers, `configUpdate` is for communicating responses, start clear.
    work->worklets.front()->output.configUpdate.clear();
    // Copy flags from work->input to first worklet's output. TODO: IDK why
    work->worklets.front()->output.flags = work->input.flags;

    if (mSignalledError || mSignalledOutputEos) {
        work->result = C2_BAD_VALUE;
        return;
    }

    bool endOfStream = GetFlagValue(work->input.flags, C2FrameData::FLAG_END_OF_STREAM);
    size_t inOffset = 0u;
    size_t inSize = 0u;
    // mDummyReadView provided by SimpleC2Component that just returns C2_NO_INIT.
    // (TODO: Why? To avoid null or optional?)
    C2ReadView readView = mDummyReadView;
    if (!work->input.buffers.empty()) {
        // There is work to be done.
        readView = work->input.buffers[0]->data().linearBlocks().front().map().get();
        inSize = readView.capacity();
        if (inSize != 0 && readView.error()) {
            // TODO I guess we only care about errors when inSize says there is something to
            // read?
            ALOGE("read view map failed %d", readView.error());
            work->result = C2_CORRUPTED;
            return;
        }
    }
    if (inSize == 0) {
        // TODO Why do we only fill empty work when inSize is 0?
        fillEmptyWork(work);
        // TODO Why do we only set EOS inside of this?
        if (endOfStream) {
            mSignalledOutputEos = true;
            ALOGV("signalled EOS");
        }
        return;
    }

    ALOGV("in buffer attr. size %zu timestamp %d frameindex %d", inSize,
          (int)work->input.ordinal.timestamp.peeku(), (int)work->input.ordinal.frameIndex.peeku());

    // TODO Why do this? inOffset is always 0 at this point.
    const uint8_t* data = readView.data() + inOffset;
    auto input_span = absl::Span(data, inSize);
    mIamfDecoder->Decode(input_span);
    // The first time that IsDescriptorProcessingComplete returns true, we update config.
    if (!mDescriptorProcessingComplete) {
        if (mIamfDecoder->IsDescriptorProcessingComplete()) {
            // First time we are seeing descriptor processing as complete.
            mDescriptorProcessingComplete = true;
            // Here we should get the sample rate info and Layout and update.
            uint32_t sample_rate;
            if (!mIamfDecoder->GetSampleRate(sample_rate).ok()) {
                ALOGE("Failed to get sample rate");
                mSignalledError = true;
                work->result = C2_CORRUPTED;
                return;
            }
            C2StreamSampleRateInfo::output sampleRateInfo(0u, sample_rate);
            // TODO get the actual rendered Layout, convert to channelmask, update
            // iamf_tools::api::OutputLayout actual_layout = mIamfDecoder->GetOutputLayout();
            // uint32_t actual_channel_mask =
            //         c2_soft_iamf_internal::GetAndroidChannelMask(actual_layout);
            // C2StreamChannelMaskInfo::output channelMaskInfo(0u, actual_channel_mask);
            std::vector<std::unique_ptr<C2SettingResult>> failures;
            // c2_status_t err = mIntf->config({&sampleRateInfo, &channelMaskInfo}, C2_MAY_BLOCK,
            // &failures);
            c2_status_t err = mIntf->config({&sampleRateInfo}, C2_MAY_BLOCK, &failures);
            if (err == OK) {
                work->worklets.front()->output.configUpdate.push_back(
                        C2Param::Copy(sampleRateInfo));
                // work->worklets.front()->output.configUpdate.push_back(C2Param::Copy(channelMaskInfo));
            } else {
                ALOGE("Config Update failed");
                mSignalledError = true;
                work->result = C2_CORRUPTED;
                return;
            }
        }
    }

    // TODO Do we need to do something like this?  (From Opusdec)
    // mSamplesToDiscard is set in 3 places to be mCodecDelay
    // if (work->input.ordinal.timestamp.peeku() == 0) mSamplesToDiscard = mCodecDelay;

    if (mIamfDecoder->IsTemporalUnitAvailable()) {
        // Get writing block
        std::shared_ptr<C2LinearBlock> block;
        c2_status_t err =
                pool->fetchLinearBlock(kMaxNumSamplesPerBuffer * kMaxChannels * sizeof(int16_t),
                                       {C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE}, &block);
        if (err != C2_OK) {
            ALOGE("fetchLinearBlock for Output failed with status %d", err);
            work->result = C2_NO_MEMORY;
            return;
        }
        C2WriteView wView = block->map().get();
        if (wView.error()) {
            ALOGE("write view map failed %d", wView.error());
            work->result = C2_CORRUPTED;
            return;
        }

        // absl::Status status = mIamfDecoder->GetOutputTemporalUnit(std::vector<std::vector<int32_t>> &);
    }
}

c2_status_t C2SoftIamfDec::drain(uint32_t drainMode, const std::shared_ptr<C2BlockPool>& pool) {
    // TODO: if (mIamfDecoder->Flush) {

    (void)pool;
    if (drainMode == NO_DRAIN) {
        ALOGW("drain with NO_DRAIN: no-op");
        return C2_OK;
    }
    if (drainMode == DRAIN_CHAIN) {
        ALOGW("DRAIN_CHAIN not supported");
        return C2_OMITTED;
    }

    return C2_OK;
}

class C2SoftIamfDecFactory : public C2ComponentFactory {
  public:
    C2SoftIamfDecFactory()
        : mHelper(std::static_pointer_cast<C2ReflectorHelper>(
                  GetCodec2PlatformComponentStore()->getParamReflector())) {}

    virtual c2_status_t createComponent(c2_node_id_t id,
                                        std::shared_ptr<C2Component>* const component,
                                        std::function<void(C2Component*)> deleter) override {
        *component = std::shared_ptr<C2Component>(
                new C2SoftIamfDec(COMPONENT_NAME, id,
                                  std::make_shared<C2SoftIamfDec::IntfImpl>(mHelper)),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id, std::shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = std::shared_ptr<C2ComponentInterface>(
                new SimpleInterface<C2SoftIamfDec::IntfImpl>(
                        COMPONENT_NAME, id, std::make_shared<C2SoftIamfDec::IntfImpl>(mHelper)),
                deleter);
        return C2_OK;
    }

    virtual ~C2SoftIamfDecFactory() override = default;

  private:
    std::shared_ptr<C2ReflectorHelper> mHelper;
};

}  // namespace android

__attribute__((cfi_canonical_jump_table)) extern "C" ::C2ComponentFactory* CreateCodec2Factory() {
    ALOGV("in %s", __func__);
    return new ::android::C2SoftIamfDecFactory();
}

__attribute__((cfi_canonical_jump_table)) extern "C" void DestroyCodec2Factory(
        ::C2ComponentFactory* factory) {
    ALOGV("in %s", __func__);
    delete factory;
}
