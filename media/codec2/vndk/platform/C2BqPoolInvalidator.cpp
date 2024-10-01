#include <utils/SystemClock.h>

#include <C2BqBufferPriv.h>
#include <C2BqPoolInvalidator.h>

namespace android {

namespace {
    static constexpr int64_t kBqPoolInvalidateDelayMs = 1000;
} // anonymous namespace

C2BqPoolInvalidateItem::C2BqPoolInvalidateItem(
        std::list<std::shared_ptr<C2BufferQueueBlockPool>> &&pools) : mPools(std::move(pools)) {
    if (mPools.size() > 0) {
        mNeedsInvalidate = true;
    } else {
        mNeedsInvalidate = false;
    }
}

void C2BqPoolInvalidateItem::invalidate() {
    std::list<std::shared_ptr<C2BufferQueueBlockPool>> pools;
    {
        std::unique_lock<std::mutex> l(mLock);
        if (!mNeedsInvalidate) {
            return;
        }
        std::move(mPools.begin(), mPools.end(), std::back_inserter(pools));
        mNeedsInvalidate = false;
    }
    for(auto it = pools.begin(); it != pools.end(); ++it) {
        (*it)->invalidate();
    }
}

void C2BqPoolInvalidateItem::skip() {
    std::unique_lock<std::mutex> l(mLock);
    mNeedsInvalidate = false;
    mPools.clear();
}

bool C2BqPoolInvalidateItem::needsInvalidate() {
    std::unique_lock<std::mutex> l(mLock);
    return mNeedsInvalidate;
}

std::shared_ptr<C2BqPoolInvalidator> C2BqPoolInvalidator::sInvalidator;
std::mutex C2BqPoolInvalidator::sMutex;

std::shared_ptr<C2BqPoolInvalidator> C2BqPoolInvalidator::getInstance() {
    std::unique_lock<std::mutex> l(sMutex);
    if (!sInvalidator) {
        sInvalidator = std::shared_ptr<C2BqPoolInvalidator>(new C2BqPoolInvalidator());
    }
    return sInvalidator;
}

C2BqPoolInvalidator::C2BqPoolInvalidator() : mDone(false) {
    mThread = std::thread(&C2BqPoolInvalidator::run, this);
}

C2BqPoolInvalidator::~C2BqPoolInvalidator() {
    {
        std::unique_lock<std::mutex> l(mMutex);
        mDone = true;
        mCv.notify_one();
    }
    if (mThread.joinable()) {
        mThread.join();
    }
}

void C2BqPoolInvalidator::queue(std::shared_ptr<C2BqPoolInvalidateItem> &item) {
    std::unique_lock<std::mutex> l(mMutex);
    std::pair<int64_t, std::shared_ptr<C2BqPoolInvalidateItem>> p =
            std::make_pair(::android::elapsedRealtime() + kBqPoolInvalidateDelayMs, item);
    mItems.push_back(p);
    mCv.notify_one();
}

void C2BqPoolInvalidator::run() {
    while(true) {
        int64_t nowMs = ::android::elapsedRealtime();
        std::unique_lock<std::mutex> l(mMutex);
        if (mDone) {
            break;
        }
        std::shared_ptr<C2BqPoolInvalidateItem> item;
        if (!mItems.empty()) {
            if (mItems.front().first <= nowMs) {
                item = mItems.front().second;
                mItems.pop_front();
            }
        }
        if (!item) {
            if (mItems.empty()) {
                mCv.wait(l);
            } else {
                int64_t nextMs = mItems.front().first;
                if (nextMs <= nowMs) {
                    l.unlock();
                    continue;
                } else {
                    mCv.wait_for(l, std::chrono::milliseconds(nextMs - nowMs));
                }
            }
        } else {
            l.unlock();
            item->invalidate();
        }
    }
}


} // android
