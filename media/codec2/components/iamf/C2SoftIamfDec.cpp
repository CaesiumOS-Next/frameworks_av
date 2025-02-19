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

namespace android {

namespace {

constexpr char COMPONENT_NAME[] = "c2.android.iamf.decoder";

}  // namespace

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
        addParameter(DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
                             .withConstValue(new C2ComponentAttributesSetting(
                                     C2Component::ATTRIB_IS_TEMPORAL))
                             .build());

        addParameter(
                DefineParam(mProfileLevel, C2_PARAMKEY_PROFILE_LEVEL)
                        .withDefault(new C2StreamProfileLevelInfo::input(
                                0u, C2Config::PROFILE_AAC_LC, C2Config::LEVEL_UNUSED))
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

        addParameter(DefineParam(mChannelMask, C2_PARAMKEY_CHANNEL_MASK)
                             .withDefault(new C2StreamChannelMaskInfo::output(0u, 0))
                             .withFields({C2F(mChannelMask, value).inRange(0, 4294967292)})
                             .withSetter(Setter<decltype(*mChannelMask)>::StrictValueWithNoDeps)
                             .build());
    }

  private:
    std::shared_ptr<C2StreamProfileLevelInfo::input> mProfileLevel;
    std::shared_ptr<C2StreamChannelMaskInfo::output> mChannelMask;
};

C2SoftIamfDec::C2SoftIamfDec(const char* name, c2_node_id_t id,
                             const std::shared_ptr<IntfImpl>& intfImpl)
    : SimpleC2Component(std::make_shared<SimpleInterface<IntfImpl>>(name, id, intfImpl)),
      mIntf(intfImpl) {}

C2SoftIamfDec::~C2SoftIamfDec() {
    onRelease();
}

c2_status_t C2SoftIamfDec::onInit() {
    return C2_BAD_STATE;
}

c2_status_t C2SoftIamfDec::onStop() {
    return C2_NO_INIT;
}

void C2SoftIamfDec::onReset() {
    return;
}

void C2SoftIamfDec::onRelease() {
    return;
}

c2_status_t C2SoftIamfDec::onFlush_sm() {
    return C2_NO_INIT;
}

void C2SoftIamfDec::process(const std::unique_ptr<C2Work>& work,
                            const std::shared_ptr<C2BlockPool>& pool) {
    (void)pool;  // Temporary solution to suppress unused var.
    work->result = C2_NO_INIT;
}

c2_status_t C2SoftIamfDec::drain(uint32_t drainMode, const std::shared_ptr<C2BlockPool>& pool) {
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
