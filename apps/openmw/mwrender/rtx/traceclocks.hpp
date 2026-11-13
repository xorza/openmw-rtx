#pragma once

#include <optional>

namespace MWRender
{
    /// How long since the last traced frame on each clock a frame is drawn by, in seconds
    /// (`Rtx::FrameOptions::mSinceLast` and `Rtx::FrameOptions::mWorldSinceLast`).
    struct TracedSeconds
    {
        float mHost = 0.0f;
        float mWorld = 0.0f;
    };

    /// Where the host's clock and the world's stood at the last trace. Both at once, because the two spans
    /// a frame is handed cover the same frames: one the upscaler tunes itself by, the other the time the
    /// world moved in that the eye adapts over.
    class TraceClocks
    {
    public:
        /// Stamps a traced frame at `host` on the host's clock (`Misc::LoopClock::getNow`) and `world` on
        /// the world's (the frame's simulation time, which a pause stops).
        ///
        /// @return how far each moved since the trace before, nought on the first.
        TracedSeconds trace(double host, double world);

    private:
        struct Stamp
        {
            double mHost = 0.0;
            double mWorld = 0.0;
        };

        std::optional<Stamp> mLast;
    };
}
