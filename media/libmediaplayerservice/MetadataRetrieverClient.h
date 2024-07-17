/*
**
** Copyright (C) 2008 The Android Open Source Project 
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

#ifndef ANDROID_MEDIAMETADATARETRIEVERSERVICE_H
#define ANDROID_MEDIAMETADATARETRIEVERSERVICE_H

#include <utils/Log.h>
#include <utils/threads.h>
#include <utils/List.h>
#include <utils/Errors.h>
#include <utils/KeyedVector.h>
#include <binder/IMemory.h>

#include <android/IMediaMetadataRetrieverSignal.h>
#include <media/MediaMetadataRetrieverInterface.h>


namespace android {

struct IMediaHTTPService;
class IMediaPlayerService;
class MemoryDealer;

class MetadataRetrieverClient : public BnMediaMetadataRetriever
{
public:
    MetadataRetrieverClient(const sp<IMediaPlayerService>& service, pid_t pid, int32_t connId);

    // Implements IMediaMetadataRetriever interface
    // These methods are called in IMediaMetadataRetriever.cpp?
    virtual void                    disconnect();

    virtual status_t                setDataSource(
            const sp<IMediaHTTPService> &httpService,
            const char *url,
            const KeyedVector<String8, String8> *headers);

    virtual status_t                setDataSource(int fd, int64_t offset, int64_t length);
    virtual status_t                setDataSource(const sp<IDataSource>& source, const char *mime);

    virtual bool                    isReady();
    virtual int                     addSignal(const sp<IMediaMetadataRetrieverSignal>& signal);
    virtual void                    removeSignal(int signalId);

    virtual sp<IMemory>             getFrameAtTime(
            int64_t timeUs, int option, int colorFormat, bool metaOnly);
    virtual sp<IMemory>             getImageAtIndex(
            int index, int colorFormat, bool metaOnly, bool thumbnail);
    virtual sp<IMemory>             getImageRectAtIndex(
            int index, int colorFormat, int left, int top, int right, int bottom);
    virtual sp<IMemory>             getFrameAtIndex(
            int index, int colorFormat, bool metaOnly);
    virtual sp<IMemory>             extractAlbumArt();
    virtual const char*             extractMetadata(int keyCode);

    virtual status_t                dump(int fd, const Vector<String16>& args);

    static  void                    removeSignalImpl(int signalId);
private:
    friend class MediaPlayerService;
    friend class AutoSignalSetReadyHelper;

    explicit MetadataRetrieverClient(pid_t pid);
    virtual ~MetadataRetrieverClient();

    mutable Mutex                          mLock;
    sp<MediaMetadataRetrieverBase>         mRetriever;
    pid_t                                  mPid;

    // Keep the shared memory copy of album art
    sp<IMemory>                            mAlbumArt;

    // RetrieverSignal class is a Bp retriever signal which is related with APP Bn retriever signal.
    // It would be used to notify APP the retriever server is ready.
    class RetrieverSignal : public RefBase {
    public:
        class DeathNotifier : public IBinder::DeathRecipient {
            public:
                DeathNotifier(RetrieverSignal &parent) : mParent(parent) {}
                virtual ~DeathNotifier() {}
                virtual void binderDied(const wp<IBinder> &who) { return mParent.binderDied(); }
            private:
                RetrieverSignal &mParent;
        }; // End of class DeathNotifier

        RetrieverSignal(int signalId, const sp<IMediaMetadataRetrieverSignal> &signal) :
            mSignalId(signalId),
            mRetrieverSignal(signal),
            mDeathNotifier(sp<DeathNotifier>::make(*this)) {
            // set binder death notifier
            IInterface::asBinder(mRetrieverSignal)->linkToDeath(mDeathNotifier);;
        }

        virtual ~RetrieverSignal() {
            IInterface::asBinder(mRetrieverSignal)->unlinkToDeath(mDeathNotifier);
            mRetrieverSignal.clear();
            mDeathNotifier.clear();
        }

        int id() const { return mSignalId; }
        sp<IMediaMetadataRetrieverSignal>& getSignal() { return mRetrieverSignal; }

        void binderDied()
        {
            ALOGD("Retriever signal(id:%d) is death, remove the signal", id());
            return MetadataRetrieverClient::removeSignalImpl(id());
        }

    private:
        sp<IMediaMetadataRetrieverSignal> mRetrieverSignal; // The Bp retriever signal.
        sp<DeathNotifier>                 mDeathNotifier;   // The binder death notifier.
        int                               mSignalId;        // The ID of the signal.
    }; // End of class RetrieverSignal

    // Record added metadata retriever signals
    static Mutex                           sLock;
    static bool                            sReady;
    static int                             sRetrieverSignalId;
    static std::list<sp<RetrieverSignal>>  sRetrieverSignals;
};

}; // namespace android

#endif // ANDROID_MEDIAMETADATARETRIEVERSERVICE_H

