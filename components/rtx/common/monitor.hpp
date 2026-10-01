#pragma once

#include <condition_variable>
#include <mutex>
#include <utility>

#include <components/platform/thread.hpp>

namespace Rtx
{
    /// The lock between a frame and the workers it keeps, and the two waits across it — the dance
    /// and not the data, which stays with its owner. Every operation is a template on what it
    /// runs, so nothing here allocates on the frame's path. A worker that throws ends the process:
    /// nothing catches it, so `std::terminate` is called where it was thrown, before anything
    /// unwinds, and the crash catcher's report names the exception beside that thread's stack. A
    /// worker's failure is a bug or an allocation that failed; what the world supplies a worker
    /// refuses in its own results.
    class Monitor
    {
    public:
        Monitor() = default;

        /// Runs `write` under the lock and wakes nobody, answering with whatever it answered — by
        /// value, so a caller cannot hand back a reference into state the lock guarded.
        template <class Write>
        auto under(Write write)
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            return write();
        }

        /// The frame's side: runs `write` under the lock, then wakes one worker and not all,
        /// because what a `give` adds is one piece of work. A caller that adds several calls this
        /// several times, which is what wakes several. A worker given work is not idle until it has
        /// looked at it, so a wait that follows cannot take the worker's last idleness for its answer.
        template <class Write>
        void give(Write write)
        {
            under([&] {
                write();
                mIdle = false;
            });
            mToWorker.notify_one();
        }

        /// A worker's side: runs `write` under the lock, then wakes the frame.
        template <class Write>
        void hand(Write write)
        {
            under(std::move(write));
            mToFrame.notify_all();
        }

        /// The frame's side: waits until `ready`, or until no worker can make it so — none in a turn,
        /// and the last to look found nothing given, or none serving at all — and says which. A wait
        /// for what nothing will hand over ends rather than waiting for ever.
        template <class Ready>
        bool await(Ready ready)
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mToFrame.wait(lock, [&] { return ready() || (mIdle && mBusy == 0); });
            return ready();
        }

        /// A worker's whole loop: wait for work, pick it up, do it, and again until stopped. `ready`
        /// and `take` share one lock hold, or a second worker would empty the queue between them;
        /// `turn` runs with the lock released, or a bake would put the workers in single file. A
        /// stop is answered before anything is picked up, or one more turn would start that nobody
        /// is left to collect.
        ///
        /// @param ready whether there is anything to do. Under the lock.
        /// @param take what to pick up. Under the same lock hold, so nothing can take it first.
        /// @param turn what to do about it, handed the stop token so a long turn can give up part
        ///        way. Outside the lock, and it reaches this monitor again for what it hands back.
        template <class Ready, class Take, class Turn>
        void serve(const Platform::StopToken& stop, Ready ready, Take take, Turn turn)
        {
            // Under the lock, or the wake could land between a worker's check and its wait.
            const Platform::StopCallback woken(stop, [this] {
                const std::lock_guard<std::mutex> lock(mMutex);
                mToWorker.notify_all();
            });

            bool busy = false;
            for (;;)
            {
                {
                    std::unique_lock<std::mutex> lock(mMutex);
                    if (busy)
                    {
                        busy = false;
                        if (--mBusy == 0)
                            mToFrame.notify_all();
                    }

                    mToWorker.wait(lock, [&] {
                        if (stop.stopRequested() || ready())
                            return true;

                        // Idle is said under the lock it is read under, and once per wait.
                        if (!mIdle)
                        {
                            mIdle = true;
                            mToFrame.notify_all();
                        }
                        return false;
                    });

                    if (stop.stopRequested())
                    {
                        mIdle = true;
                        mToFrame.notify_all();
                        return;
                    }

                    ++mBusy;
                    busy = true;
                    take();
                }

                turn(stop);
            }
        }

    private:
        std::mutex mMutex;

        std::condition_variable mToWorker;
        std::condition_variable mToFrame;

        /// Whether the last worker to look found nothing ready since the last `give`, and how many are
        /// in a turn: no worker can hand anything over where the first holds and none is busy. Until
        /// a worker serves, none can.
        bool mIdle = true;
        int mBusy = 0;
    };
}
