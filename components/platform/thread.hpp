#pragma once

#include <chrono>
#include <cstddef>
#include <exception>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

/// What each system names and measures of a thread, and how every thread here starts: a
/// `std::jthread`, named, under the terminate handler of the thread that made it.
namespace Platform
{
    /// How much of a thread's name every system keeps: Linux's limit, applied everywhere, so a thread
    /// reads the same in a report from any system.
    inline constexpr std::size_t sThreadNameLength = 15;

    /// Names the calling thread `name`, as a debugger, a profiler and a crash report show it, cut to
    /// `sThreadNameLength`.
    void nameThisThread(std::string_view name);

    /// The calling thread's name, as `nameThisThread` left it, or empty where it has none.
    std::string nameOfThisThread();

    /// How long the calling thread ran on a CPU since it started, in its own code and in the system's for
    /// it: what a pool of workers is busy for, which a wall clock cannot tell from a wait.
    std::chrono::nanoseconds cpuTimeOfThisThread();

    /// Sleeps `period`, or until a stop is asked of `stop`, and says whether it slept the whole of it:
    /// a periodic thread's wait, which a stop ends at once rather than a period later. A token with no
    /// stop state sleeps the period out.
    bool sleepUnlessStopped(const std::stop_token& stop, std::chrono::nanoseconds period);

    /// A thread running `work`, handed its `std::stop_token` where it takes one.
    ///
    /// **Every thread starts named, and under the terminate handler of the thread that made it.**
    /// MSVC keeps that handler per thread and starts each new one on the default, which aborts and
    /// says nothing; elsewhere the process has one and setting it changes nothing. What needs the
    /// copy is a test's death handler, set on the thread that runs the statement. The crash catcher
    /// sets its own on every thread the loader starts, and needs none.
    template <class Work>
    std::jthread startThread(std::string_view name, Work work)
    {
        return std::jthread([name = std::string(name), onTerminate = std::get_terminate(), work = std::move(work)](
                                std::stop_token stop) mutable {
            std::set_terminate(onTerminate);
            nameThisThread(name);
            if constexpr (std::is_invocable_v<Work&, std::stop_token>)
                work(std::move(stop));
            else
                work();
        });
    }
}
