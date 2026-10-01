#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
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
        friend bool sleepUnlessStopped(const StopToken& stop, std::chrono::nanoseconds period);

        explicit StopToken(std::shared_ptr<StopState> state);

        std::shared_ptr<StopState> mState;
    };

    /// Runs `onStop` once a stop is asked of `token`'s thread, on the thread that asks, or at once
    /// where it was asked already: how a wait on a condition variable hears of the stop. Its
    /// destruction waits out a call in progress, so `onStop` may reach what outlives this.
    ///
    /// **`onStop` runs under the token's own lock**, so it must not make or drop another callback
    /// of the same token, which `std::stop_callback` would allow: that waits on the lock it holds.
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

    /// Sleeps `period`, or until a stop is asked of `stop`'s thread, and says whether it slept the
    /// whole of it: a periodic thread's wait, which a stop ends at once rather than a period later.
    /// A token made by default sleeps the period out.
    bool sleepUnlessStopped(const StopToken& stop, std::chrono::nanoseconds period);

    /// Names the calling thread `name`, as a debugger, a profiler and a crash report show it. Linux
    /// keeps fifteen characters of it, and a name longer is cut there.
    void nameThisThread(std::string_view name);

    /// The calling thread's name, as `nameThisThread` left it, or empty where it has none.
    std::string nameOfThisThread();

    /// A thread running `work`, handed a `StopToken` where it takes one. Destroyed or assigned
    /// over, it asks the work to stop and joins it.
    ///
    /// **Every thread starts named, and under the terminate handler of the thread that made it.**
    /// MSVC keeps that handler per thread and starts each new one on the default, which aborts and
    /// says nothing; elsewhere the process has one and setting it changes nothing. So a failure on
    /// any thread ends the way it would have on its maker's: in the crash catcher's report, or a
    /// test's death handler.
    class Thread
    {
    public:
        Thread() = default;

        template <class Work>
        Thread(std::string_view name, Work work)
            : mState(makeState())
        {
            mThread = std::thread([name = std::string(name), onTerminate = std::get_terminate(), work = std::move(work),
                                      stop = StopToken(mState)]() mutable {
                std::set_terminate(onTerminate);
                nameThisThread(name);
                if constexpr (std::is_invocable_v<Work&, StopToken>)
                    work(stop);
                else
                    work();
            });
        }

        ~Thread() { stop(); }

        Thread(Thread&& other) noexcept = default;
        Thread& operator=(Thread&& other) noexcept;

        bool joinable() const { return mThread.joinable(); }

        /// Asks the work to stop and joins it. Nothing where nothing runs. Never from the thread
        /// itself, which would wait on its own end.
        void stop();

    private:
        static std::shared_ptr<StopState> makeState();

        std::shared_ptr<StopState> mState;
        std::thread mThread;
    };
}
