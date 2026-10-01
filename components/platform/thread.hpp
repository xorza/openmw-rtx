#pragma once

#include <functional>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

/// A thread that is asked to stop and joined when it goes, and the token its work asks: what
/// `std::jthread` and `<stop_token>` are. The libc++ of Xcode 16 keeps those behind
/// `-fexperimental-library`, which libc++ dropped only in LLVM 20, so one implementation over
/// `std::thread` serves every system.
namespace Platform
{
    struct StopState;

    /// Whether a stop was asked of the thread this came from. A copy asks the same thread, and a
    /// token made by default is never stopped.
    class StopToken
    {
    public:
        StopToken() = default;

        bool stopRequested() const;

    private:
        friend class StopCallback;
        friend class Thread;

        explicit StopToken(std::shared_ptr<StopState> state);

        std::shared_ptr<StopState> mState;
    };

    /// Runs `onStop` once a stop is asked of `token`'s thread, on the thread that asks, or at once
    /// where it was asked already: how a wait on a condition variable hears of the stop. Its
    /// destruction waits out a call in progress, so `onStop` may reach what outlives this.
    class StopCallback
    {
    public:
        StopCallback(const StopToken& token, std::function<void()> onStop);
        ~StopCallback();

        StopCallback(const StopCallback&) = delete;
        StopCallback& operator=(const StopCallback&) = delete;

    private:
        friend struct StopState;

        std::shared_ptr<StopState> mState;
        std::function<void()> mOnStop;
    };

    /// A thread running `work`, handed a `StopToken` where it takes one. Destroyed or assigned
    /// over, it asks the work to stop and joins it.
    class Thread
    {
    public:
        Thread() = default;

        template <class Work>
        explicit Thread(Work work)
            : mState(makeState())
        {
            if constexpr (std::is_invocable_v<Work&, StopToken>)
                mThread = std::thread([work = std::move(work), stop = StopToken(mState)]() mutable { work(stop); });
            else
                mThread = std::thread(std::move(work));
        }

        ~Thread() { stop(); }

        Thread(Thread&& other) noexcept = default;
        Thread& operator=(Thread&& other) noexcept;

        bool joinable() const { return mThread.joinable(); }

        /// Asks the work to stop and joins it. Nothing where nothing runs.
        void stop();

    private:
        static std::shared_ptr<StopState> makeState();

        std::shared_ptr<StopState> mState;
        std::thread mThread;
    };
}
