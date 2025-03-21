/*
 * Copyright 2015 The Android Open Source Project
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

#ifndef PERSISTENT_SURFACE_H_

#define PERSISTENT_SURFACE_H_

#include <android/binder_auto_utils.h>
#include <android/binder_libbinder.h>
#include <binder/Parcel.h>
#include <hidl/HidlSupport.h>
#include <hidl/HybridInterface.h>
#include <gui/IGraphicBufferProducer.h>
#include <media/stagefright/foundation/ABase.h>

namespace android {

struct PersistentSurface : public RefBase {

    enum SurfaceType : int {
        TYPE_UNKNOWN = 0,
        TYPE_HIDL,
        TYPE_AIDL,
        TYPE_HAL,
    };

    PersistentSurface() {}

    // create a persistent surface in HIDL
    PersistentSurface(
            const sp<IGraphicBufferProducer>& bufferProducer,
            const sp<hidl::base::V1_0::IBase>& hidlTarget) :
        mBufferProducer(bufferProducer),
        mHidlTarget(hidlTarget),
        mAidlTarget(nullptr),
        mHalSurface(nullptr),
        mType(TYPE_HIDL) {}

    // create a persistent surface in AIDL
    PersistentSurface(
            const sp<IGraphicBufferProducer>& bufferProducer,
            const ::ndk::SpAIBinder& aidlTarget) :
        mBufferProducer(bufferProducer),
        mHidlTarget(nullptr),
        mAidlTarget(aidlTarget),
        mHalSurface(nullptr),
        mType(TYPE_AIDL) {}

    // create a persistnet surface from HAL interface
    PersistentSurface(
            const ::ndk::SpAIBinder& aidlTarget) :
        mBufferProducer(nullptr),
        mHidlTarget(nullptr),
        mAidlTarget(nullptr),
        mHalSurface(aidlTarget),
        mType(TYPE_HAL) {}

    sp<IGraphicBufferProducer> getBufferProducer() const {
        // TYPE_HAL does not have BufferProducer.
        // extract window directly from mHalSurface in the case.
        return mBufferProducer;
    }

    SurfaceType getType() const {
        return mType;
    }

    sp<hidl::base::V1_0::IBase> getHidlTarget() const {
        return mHidlTarget;
    }

    ::ndk::SpAIBinder getAidlTarget() const {
        return mAidlTarget;
    }

    ::ndk::SpAIBinder getHalSurface() const {
        return mHalSurface;
    }

    status_t writeToParcel(Parcel *parcel) const {
        bool parceled = false;
        bool invalidSurface = false;
        switch (mType) {
            case TYPE_HIDL: {
                parcel->writeInt32(TYPE_HIDL);
                parcel->writeStrongBinder(IInterface::asBinder(mBufferProducer));
                HalToken token;
                bool result = createHalToken(mHidlTarget, &token);
                parcel->writeBool(result);
                if (result) {
                    parcel->writeByteArray(token.size(), token.data());
                    parceled = true;
                }
            }
            break;
            case TYPE_AIDL: {
                parcel->writeInt32(TYPE_AIDL);
                parcel->writeStrongBinder(IInterface::asBinder(mBufferProducer));
                AIBinder *binder = mAidlTarget.get();
                if (binder != nullptr) {
                    ::android::sp<::android::IBinder> intf = AIBinder_toPlatformBinder(binder);
                    if (intf) {
                        parcel->writeBool(true);
                        parcel->writeStrongBinder(intf);
                        parceled = true;
                        break;
                    }
                }
                parcel->writeBool(false);
            }
            break;
            case TYPE_HAL: {
                parcel->writeInt32(TYPE_HAL);
                AIBinder *binder = mHalSurface.get();
                if (binder != nullptr) {
                    ::android::sp<::android::IBinder> intf = AIBinder_toPlatformBinder(binder);
                    if (intf) {
                        parcel->writeBool(true);
                        parcel->writeStrongBinder(intf);
                        parceled = true;
                        break;
                    }
                }
                parcel->writeBool(false);
            }
            break;
            default: {
                invalidSurface = true;
                parcel->writeInt32(TYPE_UNKNOWN);
            }
            break;
        }

        if (!parceled) {
            if (invalidSurface) {
                ALOGW("surface type invalid");
            } else {
                ALOGE("binder is not parceled %d", mType);
            }
        }
        return NO_ERROR;
    }

    status_t readFromParcel(const Parcel *parcel) {
        mHidlTarget.clear();
        mAidlTarget.set(nullptr);
        mHalSurface.set(nullptr);
        SurfaceType type = static_cast<SurfaceType>(parcel->readInt32());
        switch (type) {
            case TYPE_HIDL: {
                mType = TYPE_HIDL;
                mBufferProducer = interface_cast<IGraphicBufferProducer>(
                        parcel->readStrongBinder());
                bool haveHidlTarget = parcel->readBool();
                if (haveHidlTarget) {
                    std::vector<uint8_t> tokenVector;
                    parcel->readByteVector(&tokenVector);
                    HalToken token = HalToken(tokenVector);
                    mHidlTarget = retrieveHalInterface(token);
                    deleteHalToken(token);
                }
            }
            break;
            case TYPE_AIDL: {
                mType = TYPE_AIDL;
                mBufferProducer = interface_cast<IGraphicBufferProducer>(
                        parcel->readStrongBinder());
                bool haveAidlTarget = parcel->readBool();
                if (haveAidlTarget) {
                    ::android::sp<::android::IBinder> intf = parcel->readStrongBinder();
                    AIBinder *ndkBinder = AIBinder_fromPlatformBinder(intf);
                    if (ndkBinder) {
                        mAidlTarget.set(ndkBinder);
                    }
                }
            }
            break;
            case TYPE_HAL: {
                mType = TYPE_HAL;
                bool haveHalSurface = parcel->readBool();
                if (haveHalSurface) {
                    ::android::sp<::android::IBinder> intf = parcel->readStrongBinder();
                    AIBinder *ndkBinder = AIBinder_fromPlatformBinder(intf);
                    if (ndkBinder) {
                        mHalSurface.set(ndkBinder);
                    }
                }
            }
            break;
            default: {
                mType = TYPE_UNKNOWN;
            }
            break;
        }
        return NO_ERROR;
    }

private:
    sp<IGraphicBufferProducer> mBufferProducer;
    sp<hidl::base::V1_0::IBase> mHidlTarget;
    ::ndk::SpAIBinder mAidlTarget;
    ::ndk::SpAIBinder mHalSurface;
    SurfaceType mType;

    DISALLOW_EVIL_CONSTRUCTORS(PersistentSurface);
};

}  // namespace android

#endif  // PERSISTENT_SURFACE_H_
