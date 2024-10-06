/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
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
        LOG(INFO) << "Failed to load library:" << kLibPath << dlerror();
        return;
    }
    else {
	LOG(INFO) << "IHal load library: success" << kLibPath ;
    }
    auto registerInterface = reinterpret_cast<void (*)()>(dlsym(libHandle, "registerInterface"));
    if (registerInterface == nullptr) {
        LOG(INFO) << "Failed to find symbol(registerInterface): error: " << dlerror();
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
