#pragma once

#include <cstdint>
#include <optional>

#include <components/rtx/renderer/framespend.hpp>

namespace RtxTool
{
    /// **How long a stop waits on a world that stopped arriving, in milliseconds of the wall.** A
    /// settled walk adopts a cell a frame and an unsettled one whenever the reader hands one over:
    /// the deck's whole band, four hundred and twenty frames of it, stood in seven seconds. Ten
    /// seconds with no cell is a reader that is stuck or a ring that leaves cells it never asks for,
    /// and not a slow one.
    inline constexpr double sStallMs = 10000.0;

    /// What one frame tells the measuring window: the facts of `MWRender::FrameReport` it decides by.
    struct WindowFrame
    {
        /// The frame drew the whole world (`FrameReport::isWhole`).
        bool mWhole = false;

        /// The world stood paused under the frame.
        bool mPaused = false;

        /// How many cells the ring still has to stand.
        std::uint32_t mCellsToStand = 0;

        /// How many meshes the frame's placement brought.
        std::uint32_t mArrivedMeshes = 0;

        /// What the frame spent, `Timing::Frame` being its wall time since the frame before.
        Rtx::FrameSpend mSpend;
    };

    /// **Which frames of a stop are measured, and which span each measured frame time closes**: the
    /// rules every figure `bench` quotes is selected by, apart from the world they are applied in, so
    /// they can be held by a test.
    ///
    /// A stop is measured from the frame after one that drew the whole world, plus the warm-up it
    /// asked for. Never by a count of frames from its first: a ring adopts one cell a frame, so a
    /// reach of ten cells is some four hundred frames before its last cell stands. The wait lasts as
    /// long as the world comes nearer — a frame that leaves fewer cells to stand than any before it
    /// — and fails after `sStallMs` of frames that brought none. Paused frames ahead of the
    /// measurement count toward neither the wait nor the warm-up, and fail it after `pauseLimit`
    /// of them, except in a session somebody plays, whose pauses are theirs.
    class MeasureWindow
    {
    public:
        /// What a frame came to.
        enum class Outcome
        {
            /// Ahead of the measurement, waiting for the world or warming up.
            Ahead,

            /// A measured frame.
            Measured,

            /// The world stopped arriving for `sStallMs`: the stop cannot be measured.
            Stalled,

            /// The world stood paused for longer than the pause limit ahead of the measurement.
            PausedTooLong,
        };

        struct Taken
        {
            Outcome mOutcome = Outcome::Ahead;

            /// This frame is the first measured one.
            bool mOpened = false;

            /// This frame is the first paused one ahead of the measurement: the moment to name what
            /// paused the world.
            bool mFirstPause = false;

            /// The measured frame's number, counted from one, where `mOutcome` is `Measured`.
            std::uint32_t mDrawn = 0;

            /// **This frame's wall time closes the span the frame before it worked in.**
            /// `Timing::Frame` runs from the last frame's opening to this one's, so what it holds is
            /// the game's update this frame arrived through and the renderer's work of the frame
            /// before. So the closed span is the frame before's figures with this frame's
            /// `Frame`, `Update` and `Sleep`, and the meshes the frame before brought.
            Rtx::FrameSpend mClosed;
            std::uint32_t mClosedArrived = 0;
        };

        MeasureWindow() = default;

        /// @param warmup the frames to run after the world stood whole.
        /// @param pauseLimit the paused frames ahead of the measurement that fail the stop.
        /// @param played a session somebody plays: its pauses count as running, and it never stalls.
        MeasureWindow(std::uint32_t warmup, std::uint32_t pauseLimit, bool played);

        /// Takes one traced frame.
        Taken take(const WindowFrame& frame);

        /// Which of the measured frames the frame about to be taken is, counted from nought, or
        /// nothing while it is a frame ahead of them.
        std::optional<std::uint32_t> getMeasuredIndex() const;

        /// Whether a frame taken was measured: the measurement has opened.
        bool isOpen() const { return mMeasuredFrom.has_value(); }

        /// Frames taken since the stop began, those ahead of the measurement included.
        std::uint32_t getSeen() const { return mSeen; }

        /// What the wait came to, for the stop's note and its failure.
        std::uint32_t getWaited() const { return mWaited; }
        std::uint32_t getLeastToStand() const { return mLeastToStand; }
        double getStalledMs() const { return mStalledMs; }
        std::uint32_t getWarmup() const { return mWarmup; }
        std::uint32_t getWarmedPaused() const { return mWarmedPaused; }

    private:
        std::uint32_t mWarmup = 0;
        std::uint32_t mPauseLimit = 0;
        bool mPlayed = false;

        std::uint32_t mSeen = 0;
        bool mWhole = false;
        std::uint32_t mWaited = 0;
        std::uint32_t mLeastToStand = 0;
        double mStalledMs = 0.0;
        std::uint32_t mWarmedRan = 0;
        std::uint32_t mWarmedPaused = 0;
        std::optional<std::uint32_t> mMeasuredFrom;

        Rtx::FrameSpend mPendingSpend;
        std::uint32_t mPendingArrived = 0;
    };
}
