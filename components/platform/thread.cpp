#include "thread.hpp"

#include <atomic>
#include <cassert>
#include <condition_variable>
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

        /// What `sleepUnlessStopped` waits on, under `mMutex`.
        std::condition_variable mStopped;

        void request()
        {
            {
                const std::lock_guard lock(mMutex);
                if (mRequested.exchange(true))
                    return;
                for (StopCallback* callback : mCallbacks)
                    callback->mOnStop();
            }
            mStopped.notify_all();
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

    bool sleepUnlessStopped(const StopToken& stop, const std::chrono::nanoseconds period)
    {
        if (stop.mState == nullptr)
        {
            std::this_thread::sleep_for(period);
            return true;
        }

        std::unique_lock lock(stop.mState->mMutex);
        return !stop.mState->mStopped.wait_for(lock, period, [&] { return stop.mState->mRequested.load(); });
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

        assert(mThread.get_id() != std::this_thread::get_id() && "a thread asked to join itself");
        mState->request();
        mThread.join();
    }

    std::shared_ptr<StopState> Thread::makeState()
    {
        return std::make_shared<StopState>();
    }
}
