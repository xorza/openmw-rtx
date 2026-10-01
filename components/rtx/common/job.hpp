#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <future>
#include <string_view>
#include <utility>

#include <components/platform/thread.hpp>

#include "jobprogress.hpp"

namespace Rtx
{
    /// One task on a thread of its own: started once, counted as it goes, its end and what it threw
    /// waited for from any thread, and asked to stop when this goes — the work hears it through the
    /// token it is handed, and should give up at the next step rather than finish. Declare it last
    /// in whatever owns it, for the reason `Rtx::Worker` gives.
    class Job
    {
    public:
        Job() = default;

        /// Stops the work and joins it.
        ~Job() = default;

        Job(const Job&) = delete;
        Job& operator=(const Job&) = delete;

        /// Runs `work(stop, job)` on a thread named `name`, where `work` calls `job.advance()` for
        /// each of the `count` steps it makes. Once, and of one step at least: a job of none could
        /// not say it is unfinished.
        template <class Work>
        void start(std::string_view name, const std::uint32_t count, Work work)
        {
            assert(count > 0 && "a job of no steps");
            assert(!mThread.joinable() && "a job started twice");
            mCount = count;
            std::promise<void> promise;
            mDone = promise.get_future().share();
            mThread = Platform::Thread(name,
                [this, work = std::move(work), promise = std::move(promise)](const Platform::StopToken& stop) mutable {
                    // Nothing after the work inside the `try`: for work that always throws, MSVC
                    // calls whatever follows it unreachable, and the build treats that as an error.
                    std::exception_ptr failed;
                    try
                    {
                        work(stop, *this);
                    }
                    catch (...)
                    {
                        failed = std::current_exception();
                    }

                    if (failed != nullptr)
                        promise.set_exception(failed);
                    else
                        promise.set_value();
                });
        }

        /// One more step made. From any thread the work runs on.
        void advance() { mMade.fetch_add(1, std::memory_order_relaxed); }

        /// Blocks until the work has returned, and throws what it threw. Every call after the first
        /// answers at once; each is a happens-before edge with everything the work wrote.
        void wait() const { mDone.get(); }

        /// Waits `patience` at most: the whole count and `wait`'s answer where the work returned in
        /// it, and the steps made so far, short of the count, where it did not.
        JobProgress waitFor(const std::chrono::milliseconds patience) const
        {
            if (mDone.wait_for(patience) == std::future_status::ready)
            {
                wait();
                return JobProgress{ .mMade = mCount, .mCount = mCount };
            }

            const std::uint32_t made = mMade.load(std::memory_order_relaxed);
            return JobProgress{ .mMade = std::min(made, mCount - 1), .mCount = mCount };
        }

    private:
        std::uint32_t mCount = 0;
        std::atomic<std::uint32_t> mMade{ 0 };

        /// Shared, because a shared future answers every time it is asked and a plain one once.
        std::shared_future<void> mDone;

        /// Last, so it is joined before anything the work writes goes.
        Platform::Thread mThread;
    };
}
