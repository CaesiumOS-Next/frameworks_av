/*
** Copyright 2024, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "main_audio_extension"

#include <cstdlib>
#include <ctime>
#include <utility>
#include <vector>
#include <dlfcn.h>

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_process.h>
void registerIHalAdapterVendorExtension() {
    constexpr char kLibPath[] = "libaudiohalvendorextn.so";
    void* libHandle = dlopen(kLibPath, RTLD_NOW | RTLD_NODELETE);
    if (libHandle == nullptr) {
        LOG(ERROR) << "Failed to load library:" << kLibPath << dlerror();
        return;
    }
    else {
	LOG(ERROR) << "IHal load library: success" << kLibPath ;
    }
    auto registerInterface = reinterpret_cast<void (*)()>(dlsym(libHandle, "registerInterface"));
    if (registerInterface == nullptr) {
        LOG(ERROR) << "Failed to find symbol(registerInterface): error: " << dlerror();
        return;
    }
    registerInterface();
}

int main(int argc __unused, char** argv __unused) {
    // Random values are used in the implementation.
    std::srand(std::time(nullptr));
    // libbase log level by default
    android::base::SetMinimumLogSeverity(::android::base::INFO);

    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();

    registerIHalAdapterVendorExtension();
    LOG(INFO) << "Successfully registered IHaladaptervendorextension ";
    ABinderProcess_joinThreadPool();
    return -1;  // failure
}
