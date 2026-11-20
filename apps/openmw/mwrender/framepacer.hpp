#pragma once

#include <chrono>

#include "framesaside.hpp"

namespace Misc
{
    class LoopClock;
}

namespace MWRender
{
    /// **How the game's frames are paced**: the frame-rate limit they are held to, the host's clock each one moves on,
    /// and what stood aside between two frames of the world. Every frame the game opens, the loop's and the
    /// interface's own, opens through this, so the two kinds cannot move the clock by two rules. `Renderer` holds it
    /// and opens a frame through it once the frame's heartbeat and its wait for the screen are done.
    class FramePacer
    {
    public:
        using Duration = std::chrono::steady_clock::duration;
        using TimePoint = std::chrono::steady_clock::time_point;

        /// The host's clock, handed over once before the first frame, which outlives this.
        void setClock(Misc::LoopClock& clock) { mClock = &clock; }

        /// What `setClock` handed over. Asserts that it has.
        const Misc::LoopClock& getClock() const;

        /// `[Video] framerate limit`, in frames a second, or nought for none, set at `now`: the first frame's
        /// deadline is a period on from it.
        void setFrameRateLimit(float limit, TimePoint now);

        /// What `setFrameRateLimit` handed over, nought before it has.
        float getFrameRateLimit() const { return mFrameRateLimit; }

        /// What a hold at a moment waits, and what the frame before it stood for.
        struct HoldPlan
        {
            Duration mWait{};
            Duration mStood{};
        };

        /// **What a hold at `now` waits, to a deadline a period after the last**, as upstream's limiter
        /// (`Misc::FrameRateLimiter`) schedules: a frame early waits to its deadline and stood for the period, and
        /// the next deadline is a period after that one however late the wait wakes, so a late wake is made up by
        /// the next and the rate holds; a frame late waits nothing, stood for what passed, and the schedule starts
        /// again from it, so a stall is never caught up with a burst. No limit waits nothing.
        HoldPlan planHold(TimePoint now);

        /// Holds the game to the limit until the next frame may begin (`planHold`), and answers how long the last
        /// frame stood. **The wait is precise**: SDL's (`SDL_DelayPrecise`), which sleeps on a high-resolution timer
        /// and spins the last of it, where the standard library's sleep wakes a timer tick late on Windows, 15.6 ms
        /// unless something raised the system's resolution, and missed the frame it held.
        Duration hold();

        /// How long the last `hold` slept.
        Duration getLastHold() const { return mLastHold; }

        /// Opens the loop's frame at `now`, after a hold that answered `stood`: the clock moved on by it, or by the
        /// stated step. Answers the step.
        double openLoop(Duration stood, TimePoint now);

        /// Opens a frame of the interface's own at `now`, after a hold that answered `stood`: a wall clock moved on by
        /// it, and a clock that states its step held, since it counts the loop's frames alone. Answers how long the
        /// frame stands for, nought under a stated step.
        float openNested(Duration stood, TimePoint now);

        /// The interface's frame opened last ended at `now`, drawn or standing hidden.
        void endNested(TimePoint now, FramesAside::NestedEnd end) { mAside.endNested(now, end); }

        /// The world's frame arrived: what stood aside since the last one.
        AsideSpan takeAtWorldFrame() { return mAside.takeAtWorldFrame(); }

    private:
        Misc::LoopClock* mClock = nullptr;
        float mFrameRateLimit = 0.0f;

        /// One frame at the limit, nought for none, and the deadline the last hold kept.
        Duration mPeriod{};
        TimePoint mLastDue = std::chrono::steady_clock::now();
        Duration mLastHold{};

        FramesAside mAside;
    };
}
