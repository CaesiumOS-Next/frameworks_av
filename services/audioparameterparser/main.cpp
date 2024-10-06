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
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>

#include "ParameterParser.h"

using vendor::audio::parserservice::ParameterParser;

bool registeringVendorImpl() {
    /* Libpath property needs to be defined by vendor to get the path of
     * vendorimplemenatation code lib.
     * If path and lib exists,load it else simply ignore and return as success
     * as vendor implementation is not mandatory and depends on vendors.
     */
    using namespace ::android::base;
    const std::string kLibPropName = GetProperty("ro.audio.ihaladaptervendorextension_libname","undefined");
    if (kLibPropName == "undefined") {
        LOG(ERROR) << "Failed to get property 'ro.audio.ihaladaptervendorextension_libname "
                     "ignore this error if vendor implementation is not provided";
        return false;
    } else {
       // const std::filesystem::path kLibPath = value;
        /* If libpath property is definei but file doesn't exists return fail
         */
	std::string fullLibPath = "system_ext/lib/" + kLibPropName;    
        if(!(std::filesystem::exists(fullLibPath))) {
            LOG(ERROR) << "File doesn't exist at " << fullLibPath;
            return false ;
        } else {
            void* libHandle = dlopen(fullLibPath.c_str(), RTLD_NOW | RTLD_NODELETE);
            if (libHandle == nullptr) {
                LOG(ERROR) << "Failed to load library:" << kLibPropName << dlerror();
                return false;
            } else {
                LOG(INFO) << "IHal load library: success" << kLibPropName ;
            }
            /* Vendor needs to pass registration functor pointer in vendor lib as property
             * to invoke the registration function of vendor implementations .so
             */
            const std::string kPropSymbol = GetProperty("ro.audio.ihaladaptervendorextension_symbol","undefined");
            if(kPropSymbol== "undefined") {
                LOG(ERROR) << "Failed to get property 'ro.audio.ihaladaptervendorextension_symbol'" ;
                return false;
            } else {
                auto registerInterface = reinterpret_cast<void (*)()>(dlsym(libHandle,kPropSymbol.c_str()));
                if (registerInterface == nullptr) {
                    LOG(ERROR) << "Failed to find symbol \"" << kPropSymbol << "\"";
                    return false;
                }
                registerInterface();
            }
        }
        return true;
    }
}

int main() {
    // This is a debug implementation, always enable debug logging.
    android::base::SetMinimumLogSeverity(::android::base::DEBUG);
	// If vendorimplemented interfcae resistration fails, register with default
    if(!registeringVendorImpl()) {
        auto parser = ndk::SharedRefBase::make<ParameterParser>();
        const std::string parserFqn =
        std::string().append(ParameterParser::descriptor).append("/default");
        binder_status_t status =
            AServiceManager_addService(parser->asBinder().get(), parserFqn.c_str());
        if (status != STATUS_OK) {
            LOG(ERROR) << "failed to register service for \"" << parserFqn << "\"";
        }
    } else {
        LOG(INFO) << "Successfully registered Ihalvendorextension service ";
    }
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
