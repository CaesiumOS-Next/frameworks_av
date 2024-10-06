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

#define LOG_TAG "Audio_ParameterParser"
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android-base/properties.h>
#include <mediautils/Library.h>

#include "ParameterParser.h"

using vendor::audio::parserservice::ParameterParser;

binder_status_t tryRegisteringVendorImpl() {
    /*
     * The property "ro.audio.ihaladaptervendorextension_libname" allows vendors
     * or OEMs to dynamically load a specific library
     * into this process space using dlopen.
     *
     * The property "ro.audio.ihaladaptervendorextension_symbol" identifies the symbol within
     * the dynamically loaded library using dlsym. This symbol can then be used to register the
     * "::aidl::android::hardware::audio::core::IHalAdapterVendorExtension/default"
     * with the ServiceManager.
     */
    const std::string kLibPropName =
        ::android::base::GetProperty("ro.audio.ihaladaptervendorextension_libname","");
    if (kLibPropName == "") {
        LOG(DEBUG) << kLibPropName << "property is not found";
        return STATUS_BAD_VALUE;
    }
    static std::shared_ptr<void> libHandle = android::mediautils
        ::loadLibrary(kLibPropName.c_str());
    if (libHandle == nullptr) {
        LOG(ERROR) << "Failed to load library:" << kLibPropName << dlerror();
        return STATUS_BAD_VALUE;
    }
    const std::string kPropSymbol =
        ::android::base::GetProperty("ro.audio.ihaladaptervendorextension_symbol","");
    if(kPropSymbol== "") {
        LOG(ERROR) << "Failed to get property 'ro.audio.ihaladaptervendorextension_symbol'";
        return STATUS_BAD_VALUE;
    }
    std::shared_ptr<void> untypedObject = android::mediautils::
        getUntypedObjectFromLibrary(kPropSymbol.c_str(), libHandle);
    auto registerInterface = reinterpret_cast<int(*)()>(untypedObject.get());
    if (registerInterface == nullptr) {
        LOG(ERROR) << "Failed to find symbol \"" << kPropSymbol << "\"";
        return  STATUS_BAD_VALUE;
    }
    return registerInterface();
}

int main() {
    // This is a debug implementation, always enable debug logging.
    android::base::SetMinimumLogSeverity(::android::base::DEBUG);
    if (tryRegisteringVendorImpl() != STATUS_OK)
    {
    const std::string parserFqn =
    std::string().append(::aidl::android::media::audio
        ::IHalAdapterVendorExtension::descriptor).append("/default");
    auto parser = ndk::SharedRefBase::make<ParameterParser>();
        binder_status_t status =
            AServiceManager_addService(parser->asBinder().get(), parserFqn.c_str());
        if (status != STATUS_OK) {
            LOG(ERROR) << "failed to register service for \"" << parserFqn << "\"";
        }
    }else
        LOG(INFO) << "IHalAdapterVendorExtension registered with vendor's implementation";
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
