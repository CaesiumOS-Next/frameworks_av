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

    virtual bool                    acquireResource(
            const sp<IMediaMetadataRetrieverSignal> &signal);

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

    static  void                    handleDiedSignal(int id);
    static  void                    signal();
private:
    friend class MediaPlayerService;

    explicit MetadataRetrieverClient(pid_t pid);
    virtual ~MetadataRetrieverClient();

    mutable Mutex                          mLock;
    sp<MediaMetadataRetrieverBase>         mRetriever;
    pid_t                                  mPid;

    // Keep the shared memory copy of album art
    sp<IMemory>                            mAlbumArt;

    // RetrieverSignal class is a Bp retriever signal which is related with APP Bn signal.
    // It would be used to binder-callback signal to bp retriever.
    class RetrieverSignal : public RefBase {
    public:
        class DeathNotifier : public IBinder::DeathRecipient {
        public:
            DeathNotifier(RetrieverSignal *parent) : mParent(parent) {}
            virtual ~DeathNotifier() {}
            virtual void binderDied(const wp<IBinder> &who) {
                sp<RetrieverSignal> parent = mParent.promote();
                if (parent != NULL) {
                    parent->binderDied();
                }
                return;
            }

        private:
            wp<RetrieverSignal> mParent;
        }; // End of class DeathNotifier

        RetrieverSignal(int signalId, const sp<IMediaMetadataRetrieverSignal>& signal)
            : mSignalId(signalId),
              mRetrieverSignal(signal),
              mDeathNotifier(sp<DeathNotifier>::make(this)) {
            // set binder death notifier
            IInterface::asBinder(mRetrieverSignal)->linkToDeath(mDeathNotifier);
        }

        virtual ~RetrieverSignal() {
            IInterface::asBinder(mRetrieverSignal)->unlinkToDeath(mDeathNotifier);
        }

        int id() const {
            return mSignalId;
        }

        const sp<IMediaMetadataRetrieverSignal>& getSignal() {
            return mRetrieverSignal;
        }

        void binderDied() {
            return MetadataRetrieverClient::handleDiedSignal(id());
        }

    private:
        sp<IMediaMetadataRetrieverSignal> mRetrieverSignal; // Bp retriever signal.
        sp<DeathNotifier>                 mDeathNotifier;   // Binder death notifier.
        int                               mSignalId;        // ID of the signal.
    }; // End of class RetrieverSignal

    // We make sure that the retriever getFrame series calls is
    // serialize in the metadata retriever server.
    // Serialize call requirements is to avoid media insufficient concurrency.
    //
    // Record all bp retriever calls, it would be notified once the current task is done
    static Mutex                           sLock;
    static int                             sRunningRetrieverCount;
    static int                             sRetrieverSignalId;
    static std::list<sp<RetrieverSignal>>  sRetrieverSignals;
};

}; // namespace android

#endif // ANDROID_MEDIAMETADATARETRIEVERSERVICE_H
