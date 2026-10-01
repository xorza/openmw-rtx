#include "thread.hpp"

#include <atomic>
#include <mutex>
#include <vector>

namespace Platform
{
    struct StopState
    {
        std::atomic<bool> mRequested = false;

        /// Held while the callbacks run, so one destroyed meanwhile waits until its call is over.
        std::mutex mMutex;
        std::vector<StopCallback*> mCallbacks;

        void request()
        {
            const std::lock_guard lock(mMutex);
            if (mRequested.exchange(true))
                return;
            for (StopCallback* callback : mCallbacks)
                callback->mOnStop();
        }
    };

    StopToken::StopToken(std::shared_ptr<StopState> state)
        : mState(std::move(state))
    {
    }

    bool StopToken::stopRequested() const
    {
        return mState != nullptr && mState->mRequested.load();
    }

    StopCallback::StopCallback(const StopToken& token, std::function<void()> onStop)
        : mState(token.mState)
        , mOnStop(std::move(onStop))
    {
        if (mState == nullptr)
            return;

        const std::lock_guard lock(mState->mMutex);
        if (mState->mRequested.load())
            mOnStop();
        else
            mState->mCallbacks.push_back(this);
    }

    StopCallback::~StopCallback()
    {
        if (mState == nullptr)
            return;

        const std::lock_guard lock(mState->mMutex);
        std::erase(mState->mCallbacks, this);
    }

    Thread& Thread::operator=(Thread&& other) noexcept
    {
        stop();
        mState = std::move(other.mState);
        mThread = std::move(other.mThread);
        return *this;
    }

    void Thread::stop()
    {
        if (!mThread.joinable())
            return;

        mState->request();
        mThread.join();
    }

    std::shared_ptr<StopState> Thread::makeState()
    {
        return std::make_shared<StopState>();
    }
}
