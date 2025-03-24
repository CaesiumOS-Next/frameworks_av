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
using ::std::shared_ptr;

class C2SoftIamfDec::IntfImpl : public SimpleInterface<void>::BaseParams {
  public:
    explicit IntfImpl(const shared_ptr<C2ReflectorHelper>& helper)
        : SimpleInterface<void>::BaseParams(helper, COMPONENT_NAME, C2Component::KIND_DECODER,
                                            C2Component::DOMAIN_AUDIO, MEDIA_MIMETYPE_AUDIO_IAMF) {
        noInputLatency();
        noPrivateBuffers();
        noInputReferences();
        noOutputReferences();
        noTimeStretch();
        setDerivedInstance(this);

        addParameter(DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
                             .withConstValue(new C2ComponentAttributesSetting(
                                     C2Component::ATTRIB_IS_TEMPORAL))
                             .build());

        // ===== Parameters for the input bitstream =====
        // The profile will be updated by the decoder once the DescriptorObus have all been parsed.
        // Level is unused by IAMF.
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
                             // Starting with a safe max size based on max(opus,aac,flac) + space
                             // for DescriptorObus. Opus: 960*6, Aac: 8196, Flac: 32768
                             .withConstValue(new C2StreamMaxBufferSizeInfo::input(0u, 36000))
                             .build());

        // ===== Params for the output audio =====
        // The sample rate will be determined by the content in the IAMF stream and updated once the
        // DescriptorObus are processed.
        addParameter(
                DefineParam(mSampleRate, C2_PARAMKEY_SAMPLE_RATE)
                        .withDefault(new C2StreamSampleRateInfo::output(0u, 48000))
                        // This decoder is currently PCM and Opus only.
                        // IAMF spec allows PCM with sample rates {44.1k, 16k, 32k, 48k, 96k}.
                        .withFields(
                                {C2F(mSampleRate, value).oneOf(16000, 32000, 44100, 48000, 96000)})
                        .withSetter((Setter<decltype(*mSampleRate)>::StrictValueWithNoDeps))
                        .build());
        // The ChannelMask determines what speaker layout the caller would like to target.  Only a
        // fixed set of Layouts are supported by IAMF and the requested Layout might not be present
        // in the given file.  The actual output Layout is updated once the DescriptorObus
        // are processed and can be checked by the caller.
        addParameter(
                DefineParam(mRenderedChannelMask, C2_PARAMKEY_CHANNEL_MASK)
                        .withDefault(new C2StreamChannelMaskInfo::output(0u, 0))
                        .withFields({C2F(mRenderedChannelMask, value).inRange(0, 4294967292)})
                        .withSetter(Setter<decltype(*mRenderedChannelMask)>::StrictValueWithNoDeps)
                        .build());
        // Only output audio in 16-bit ints are supported by this decoder.
        addParameter(
                DefineParam(mPcmEncodingInfo, C2_PARAMKEY_PCM_ENCODING)
                        .withDefault(new C2StreamPcmEncodingInfo::output(0u, C2Config::PCM_16))
                        .withFields({C2F(mPcmEncodingInfo, value).oneOf({C2Config::PCM_16})})
                        .withSetter((Setter<decltype(*mPcmEncodingInfo)>::StrictValueWithNoDeps))
                        .build());
    }

    uint32_t getOutputChannelMask() const { return mRenderedChannelMask->value; }

  private:
    // Params relating to the input IAMF bitstream.
    shared_ptr<C2StreamProfileLevelInfo::input> mProfileLevel;
    shared_ptr<C2StreamMaxBufferSizeInfo::input> mInputMaxBufSize;
    // Params relating to the output audio.
    shared_ptr<C2StreamChannelMaskInfo::output> mRenderedChannelMask;
    shared_ptr<C2StreamSampleRateInfo::output> mSampleRate;
    shared_ptr<C2StreamPcmEncodingInfo::output> mPcmEncodingInfo;
};

C2SoftIamfDec::C2SoftIamfDec(const char* name, c2_node_id_t id,
                             const shared_ptr<IntfImpl>& intfImpl)
    : SimpleC2Component(std::make_shared<SimpleInterface<IntfImpl>>(name, id, intfImpl)),
      mIntf(intfImpl),
      mIamfDecoder(nullptr) {}

C2SoftIamfDec::~C2SoftIamfDec() {
    onRelease();
}

void C2SoftIamfDec::initializeState() {
    mOutputFrameSizeBytes = 0;
    mSignalledError = false;
    mSignalledOutputEos = false;
    mDescriptorProcessingComplete = false;
}

c2_status_t C2SoftIamfDec::onInit() {
    initializeState();
    iamf_tools::api::OutputLayout target_layout =
            c2_soft_iamf_internal::GetIamfLayout(mIntf->getOutputChannelMask());
    StatusOr<IamfDecoder> decoder = IamfDecoder::Create(target_layout);
    if (!decoder.ok()) {
        // IamfDecoder::Create fails if it cannot create the read bit buffer.
        return C2_NO_MEMORY;
    }
    // Set to request only LE int16 samples as specified in the above params.
    decoder->ConfigureOutputSampleType(iamf_tools::api::OutputSampleType::kInt16LittleEndian);

    mIamfDecoder = std::make_unique<IamfDecoder>(*std::move(decoder));
    return C2_OK;
}

c2_status_t C2SoftIamfDec::onStop() {
    // onStop should preserve state of the decoder (in the mInf interface).
    if (mIamfDecoder) {
        (void)mIamfDecoder->Close();
        mIamfDecoder = nullptr;
    }
    initializeState();
    return C2_OK;
}

void C2SoftIamfDec::onReset() {
    // Reset should re-initialize.
    (void)onStop();
}

void C2SoftIamfDec::onRelease() {
    // onRelease, the decoder is guaranteed not to be used again, so we release
    // the decoder without worrying about resetting state.
    if (mIamfDecoder) {
        (void)mIamfDecoder->Close();  // ?
        mIamfDecoder = nullptr;
    }
}

c2_status_t C2SoftIamfDec::onFlush_sm() {
    // Discards and abandons any pending work.
    return onStop();
}

c2_status_t C2SoftIamfDec::getTemporalUnit(const std::unique_ptr<C2Work>& work,
                                           const std::shared_ptr<C2BlockPool>& pool,
                                           bool useFlushMethod) {
    // Get writing block into a Span for writing by the |mIamfDecoder|.
    shared_ptr<C2LinearBlock> block;
    c2_status_t err = pool->fetchLinearBlock(
            mOutputFrameSizeBytes, {C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE}, &block);
    if (err != C2_OK) {
        ALOGE("fetchLinearBlock for Output failed with status %d", err);
        mSignalledError = true;
        work->result = C2_NO_MEMORY;
        return;
    }
    C2WriteView wView = block->map().get();
    if (wView.error()) {
        ALOGE("write view map failed %d", wView.error());
        mSignalledError = true;
        work->result = C2_CORRUPTED;
        return;
    }
    size_t bytesWritten = 0;
    auto outputSpan = absl::Span(wView.data(), mOutputFrameSizeBytes);

    bool isDone = true;
    Status status = useFlushMethod ? mIamfDecoder->Flush(outputSpan, bytesWritten, isDone)
                                   : mIamfDecoder->GetOutputTemporalUnit(outputSpan, bytesWritten);
    if (!status.ok()) {
        ALOGE("Failed to get temporal unit.");
        mSignalledError = true;
        work->result = C2_CORRUPTED;
        return;
    }
    if (!isDone) {
        ALOGW("Temporal units remaining after flush.")
    }
    ALOGV("out buffer attr. size %zu", bytesWritten);
    work->worklets.front()->output.buffers.clear();
    work->worklets.front()->output.buffers.push_back(createLinearBuffer(block, 0, outSize));
    work->worklets.front()->output.ordinal = work->input.ordinal;

    return C2_OK;
}

void C2SoftIamfDec::process(const std::unique_ptr<C2Work>& work,
                            const shared_ptr<C2BlockPool>& pool) {
    // N.B.: Android only supports single input buffer and single worklet.

    // Initialize output work to assume OK and that we will process one worklet.
    work->result = C2_OK;
    work->workletsProcessed = 1u;
    // For output buffers, `configUpdate` is for communicating responses, start clear.
    work->worklets.front()->output.configUpdate.clear();
    // Copy flags from work->input to first worklet's output.
    work->worklets.front()->output.flags = work->input.flags;

    if (mSignalledError || mSignalledOutputEos) {
        // We already had an error or EOS signalled previously, we should not have
        // had `process` called again.
        work->result = C2_BAD_VALUE;
        return;
    }

    size_t inSize = 0u;
    // mDummyReadView provided by SimpleC2Component that just returns C2_NO_INIT.
    // It is here as a placeholder.
    C2ReadView readView = mDummyReadView;
    if (!work->input.buffers.empty()) {
        // Input buffers are not empty, so there is work to be done.
        readView = work->input.buffers[0]->data().linearBlocks().front().map().get();
        inSize = readView.capacity();
        // readView could give a capacity of 0 but that does not mean corrupted.
        if (inSize != 0 && readView.error()) {
            ALOGE("ReadView map failed %d", readView.error());
            mSignalledError = true;
            work->result = C2_CORRUPTED;
            return;
        }
    }
    // We know it's EOS of this flag is set.
    bool endOfStream = work->input.flags & C2FrameData::FLAG_END_OF_STREAM;

    if (inSize == 0) {
        // If inSize is still zero at this point, then there is no input to process.
        work->worklets.front()->output.buffers.clear();
        work->worklets.front()->output.ordinal = work->input.ordinal;
        work->workletsProcessed = 1u;
        // No more data to send to decode if inSize is 0 and EOS signalled.
        if (endOfStream) {
            mSignalledOutputEos = true;
            ALOGV("signalled EOS");
        }
        // For IAMF, we will still try to fetch a temporal unit at EOS.
        if (mIamfDecoder->IsTemporalUnitAvailable()) {
            getTemporalUnit(work, pool, /*useFlushMethod=*/true);
        }
    }

    ALOGV("in buffer attr. size %zu timestamp %d frameindex %d", inSize,
          (int)work->input.ordinal.timestamp.peeku(), (int)work->input.ordinal.frameIndex.peeku());

    const uint8_t* data = readView.data();
    auto input_span = absl::Span(data, inSize);
    mIamfDecoder->Decode(input_span);
    // The first time that IsDescriptorProcessingComplete returns true, we update config.
    if (!mDescriptorProcessingComplete && mIamfDecoder->IsDescriptorProcessingComplete()) {
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
        // The Layout used may be different than what was requested in IamfDecoder::Create
        // because of the content of the stream.  Here we get the actual Layout that will be used
        // and convert to a ChannelMask.
        StatusOr<iamf_tools::api::OutputLayout> actual_layout = mIamfDecoder->GetOutputLayout();
        if (!actual_layout.status().ok()) {
            ALOGE("Failed to get output layout.");
            mSignalledError = true;
            work->result = C2_CORRUPTED;
            return;
        }
        uint32_t actual_channel_mask = c2_soft_iamf_internal::GetAndroidChannelMask(*actual_layout);
        C2StreamChannelMaskInfo::output channelMaskInfo(0u, actual_channel_mask);
        // We collect the failures but do not use them.
        std::vector<std::unique_ptr<C2SettingResult>> failures;
        // Update the config in the params.
        c2_status_t err =
                mIntf->config({&sampleRateInfo, &channelMaskInfo}, C2_MAY_BLOCK, &failures);
        if (err == OK) {
            // Include the config update in the work for the caller to see.
            work->worklets.front()->output.configUpdate.push_back(C2Param::Copy(sampleRateInfo));
            work->worklets.front()->output.configUpdate.push_back(C2Param::Copy(channelMaskInfo));
        } else {
            ALOGE("Config Update failed");
            mSignalledError = true;
            work->result = C2_CORRUPTED;
            return;
        }
        // Calculate the max size of the memory block we need to request for output buffers.
        StatusOr<uint32_t> frameSize = mIamfDecoder->GetFrameSize();
        if (!frameSize.status().ok()) {
            ALOGE("Failed to get frame size.");
            mSignalledError = true;
            work->result = C2_CORRUPTED;
            return;
        }
        StatusOr<int> numOutputChannels = mIamfDecoder->GetNumberOfOutputChannels();
        if (!numOutputChannels.status().ok()) {
            ALOGE("Failed to get number of output channels.");
            mSignalledError = true;
            work->result = C2_CORRUPTED;
            return;
        }
        mOutputFrameSizeBytes = *frameSize * sizeof(int16_t) * *numOutputChannels;
    }

    if (mIamfDecoder->IsTemporalUnitAvailable()) {
        return getTemporalUnit(work, pool, /*useFlushMethod=*/false);
    }
}

c2_status_t C2SoftIamfDec::drain(uint32_t drainMode, const shared_ptr<C2BlockPool>& pool) {
    // Practically speaking, drain is unused.
    (void)pool;
    // SimpleC2Component
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

    virtual c2_status_t createComponent(c2_node_id_t id, shared_ptr<C2Component>* const component,
                                        std::function<void(C2Component*)> deleter) override {
        *component = shared_ptr<C2Component>(
                new C2SoftIamfDec(COMPONENT_NAME, id,
                                  std::make_shared<C2SoftIamfDec::IntfImpl>(mHelper)),
                deleter);
        return C2_OK;
    }

    virtual c2_status_t createInterface(
            c2_node_id_t id, shared_ptr<C2ComponentInterface>* const interface,
            std::function<void(C2ComponentInterface*)> deleter) override {
        *interface = shared_ptr<C2ComponentInterface>(
                new SimpleInterface<C2SoftIamfDec::IntfImpl>(
                        COMPONENT_NAME, id, std::make_shared<C2SoftIamfDec::IntfImpl>(mHelper)),
                deleter);
        return C2_OK;
    }

    virtual ~C2SoftIamfDecFactory() override = default;

  private:
    shared_ptr<C2ReflectorHelper> mHelper;
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
