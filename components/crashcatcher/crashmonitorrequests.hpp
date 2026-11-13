#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <variant>
#include <vector>

namespace Crash
{
    /// The game stood still for `mSeconds`, at the frame count `mStalledAt`, and the watch took a dump of it, or
    /// `mDumped` says it could not.
    struct AskToEnd
    {
        std::uint32_t mSeconds;
        std::uint64_t mStalledAt;
        bool mDumped;
    };

    /// Crashpad's handler returned `mResult`: the game is gone.
    struct HandlerReturned
    {
        int mResult;
    };

    /// The game has been gone for the monitor's last resort and Crashpad's handler has not returned: stuck, as one
    /// reading a game that never let it attach is.
    struct HandlerOverdue
    {
    };

    /// **What the monitor's main thread is asked to do, in order.** It is the one thread that shows a dialog, which
    /// stands for as long as the player takes over it, so neither Crashpad's handler nor the watch waits on a click.
    /// The handler runs on a worker and the watch asks here.
    using MonitorRequest = std::variant<AskToEnd, HandlerReturned, HandlerOverdue>;

    /// The requests posted to the main thread, from any thread.
    class MonitorRequests
    {
    public:
        void post(const MonitorRequest& request);

        /// The oldest request, waited for. **One that says the game is gone goes first**: a box about a game
        /// already gone asks nothing. The questions it passes stay for `takeUnasked`.
        MonitorRequest take();

        /// How many questions the game ended before, once nothing posts any more: those `take` passed for the
        /// game's end, and those the watch posted after it, as it does when a hang's dump outlasts the game. Every
        /// request goes with them.
        std::size_t takeUnasked();

    private:
        std::mutex mMutex;
        std::condition_variable mWake;
        std::vector<MonitorRequest> mRequests;
    };
}
