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
#include "ParameterParser.h"
#include <cstdlib>
#include <cutils/properties.h>
#include <dlfcn.h>
#include <filesystem>

using vendor::audio::parserservice::ParameterParser;

bool registerIHalAdapterVendorExtension() {
   char value[PROPERTY_VALUE_MAX];

    /* Libpath property needs to be defined by vendor to get the path of
     * vendorimplemenatation code lib.
     * If path and lib exists,load it else simply ignore and return as success
     * as vendor implementation is not mandatory and depends on vendors.
     */
    if(property_get("ro.audio.ihaladaptervendorextension_libname",value, "unknown")> 0) {
        const std::filesystem::path kLibPath = value;
        /* If libpath property is defined but file doesn't exists return fail
         */
        if(std::filesystem::exists(kLibPath)) {
            void* libHandle = dlopen(kLibPath.c_str(), RTLD_NOW | RTLD_NODELETE);
            if (libHandle == nullptr) {
                LOG(ERROR) << "Failed to load library:" << kLibPath << dlerror();
                return false;
            } else {
                LOG(INFO) << "IHal load library: success" << kLibPath ;
            }
            /* Vendor needs to pass registration functor pointer in vendor lib as property
             * to invoke the registration function of vendor implementations .so
             */
            if(property_get("ro.audio.ihaladaptervendorextension_servicename",value, "unknown")> 0) {
                const std::string valueStr = value;
                auto registerInterface = reinterpret_cast<void (*)()>(dlsym(libHandle,valueStr.c_str()));
                if (registerInterface == nullptr) {
                    LOG(ERROR) << "Failed to find symbol \"" << value << "\"";
                    return false;
                }
                registerInterface();
            } else {
            LOG(ERROR) << "Failed to get property 'ro.audio.ihaladaptervendorextension_service'" ;
            return false;
            }
        } else {
            LOG(ERROR) << "File doesn't exist at " << kLibPath;
            return false ;
        }
    }
    /* Reaches here only when lib loading and registration are success
     * or implementation/property are absent
     */
    return true;
}

int main() {
    // This is a debug implementation, always enable debug logging.
    android::base::SetMinimumLogSeverity(::android::base::INFO);

    auto parser = ndk::SharedRefBase::make<ParameterParser>();
    const std::string parserFqn =
            std::string().append(ParameterParser::descriptor).append("/default");
    binder_status_t status =
            AServiceManager_addService(parser->asBinder().get(), parserFqn.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << "failed to register service for \"" << parserFqn << "\"";
    }
    if(registerIHalAdapterVendorExtension()) {
        LOG(INFO) << "Bypassed/Successfully registered IHaladaptervendorextension ";
    } else {
        LOG(ERROR) << "Failed to register IHaladaptervendorextension vendor service";
    }
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
