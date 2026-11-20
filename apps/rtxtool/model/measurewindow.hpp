#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <components/rtx/renderer/framespend.hpp>

namespace RtxTool
{
    /// **How long a stop waits on a world that stopped arriving, in milliseconds of the wall.** A settled walk
    /// adopts a cell a frame and an unsettled one whenever the reader hands one over (the deck's whole band, 420
    /// frames, stood in seven seconds); ten seconds with no cell and no image nearer is a stuck reader or a ring that
    /// leaves cells it never asks for, not a slow one.
    inline constexpr double sStallMs = 10000.0;

    /// What one frame tells the measuring window: the facts of `MWRender::FrameReport` it decides by.
    struct WindowFrame
    {
        /// The frame drew the whole world (`FrameReport::isWhole`).
        bool mWhole = false;

        /// The world stood paused under the frame.
        bool mPaused = false;

        /// How many cells the ring still has to stand, and how many images are still being made
        /// (`FrameReport::mImagesPending`).
        std::uint32_t mCellsToStand = 0;
        std::uint32_t mImagesPending = 0;

        /// The frame's placement brought something to the device, which a whole frame's does not.
        bool mArriving = false;

        /// How many meshes the frame's placement brought.
        std::uint32_t mArrivedMeshes = 0;

        /// The cell the player stood in when the frame was drawn, which the window tells a
        /// crossing by, and whether the frame's placement rebuilt the scene from nothing.
        const void* mCell = nullptr;
        bool mRebuilt = false;

        /// What the frame spent, `Timing::Frame` being its wall time since the frame before.
        Rtx::FrameSpend mSpend;
    };

    /// The span a frame's arrival closes: the frame before's figures with this frame's `Frame`, `Update` and `Sleep`
    /// (`Rtx::FrameSpend::closedBy`), and the meshes the frame before brought. `Timing::Frame` runs from the last
    /// frame's opening to this one's, so it holds the game's update this frame arrived through and the renderer's
    /// work of the frame before: the span a hitch is read in.
    struct ClosedSpan
    {
        Rtx::FrameSpend mSpend;
        std::uint32_t mArrived = 0;
    };

    /// Whether a stop waits for its world to stand whole before its warm-up, or takes it as standing: a flight that
    /// goes on from the stop before (`Schedule::mContinues`), whose world is the one that stop flew through, is
    /// measured from its second frame, as a player's flight is not held up at a line nobody drew. **Not its first**:
    /// that one's wall time runs from the stop before's last frame, and holds what the session does between two stops
    /// (`Session::advance`), its report of the host's memory among it.
    enum class WorldStands : std::uint8_t
    {
        Awaited,
        Already,
    };

    /// **Which frames of a stop are measured, and which span each measured frame time closes**: the rules every
    /// figure `bench` quotes is selected by, apart from the world they are applied in, so a test can hold them.
    ///
    /// A stop is measured from the frame after one that drew the whole world, plus the warm-up it
    /// asked for. Never by a count of frames from its first: a ring adopts one cell a frame, so a
    /// reach of ten cells is some four hundred frames before its last cell stands. The wait lasts as
    /// long as the world comes nearer — a frame that leaves fewer cells to stand or fewer images to make
    /// than any before it, each of what a whole frame asks that counts down — and fails after `sStallMs`
    /// of frames that brought neither. Paused frames ahead of the
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

            /// The span this frame's arrival closed.
            ClosedSpan mClosed;

            /// The frame before stood in another cell than the one before it: a crossing, and whether its placement
            /// rebuilt the scene, which `mClosed` is the cost of. The move pulls the ring in and the read lands in
            /// the first frame drawn in the new cell, so that frame's own span is what the crossing cost.
            std::optional<bool> mClosedCrossing;
        };

        MeasureWindow() = default;

        /// @param warmup the frames to run after the world stood whole.
        /// @param pauseLimit the paused frames ahead of the measurement that fail the stop.
        /// @param played a session somebody plays: its pauses count as running, and it never stalls.
        /// @param stands whether the world is waited for or stands already, which then needs no warm-up and runs the
        /// one frame the stops change on.
        MeasureWindow(
            std::uint32_t warmup, std::uint32_t pauseLimit, bool played, WorldStands stands = WorldStands::Awaited);

        /// Takes one traced frame.
        Taken take(const WindowFrame& frame);

        /// Which of the measured frames the frame about to be taken is, counted from nought, or
        /// nothing while it is a frame ahead of them.
        std::optional<std::uint32_t> getMeasuredIndex() const;

        /// Whether a frame taken was measured: the measurement has opened.
        bool isOpen() const { return mMeasuredFrom.has_value(); }

        /// Frames taken since the stop began, those ahead of the measurement included.
        std::uint32_t getSeen() const { return mSeen; }

        /// How many frames were seen ahead of the first measured one — the count it was traced at —
        /// or nothing until the measurement opens.
        std::optional<std::uint32_t> getMeasuredFrom() const { return mMeasuredFrom; }

        /// What the wait came to, for the stop's note and its failure.
        std::uint32_t getWaited() const { return mWaited; }

        /// The frames taken since the one that drew the whole world, which is the number of the frame about to be
        /// taken in the stop's own time; or nothing while the world is still arriving. A world that stood when the
        /// window opened stood from before its first frame.
        std::optional<std::uint32_t> getSinceWhole() const
        {
            return mWhole ? std::optional<std::uint32_t>(mSeen - mStoodAt) : std::nullopt;
        }

        /// How many frames had been taken by the one that drew the whole world, its own among them: nought for a
        /// world that stood already. **Paused frames of the wait among them**, which `getWaited` leaves out.
        std::uint32_t getStoodAt() const { return mStoodAt; }

        /// What kept the world from standing whole: the fewest cells and images any frame of the wait left, and
        /// whether its last frame still brought something. What a stalled wait names.
        struct Unstood
        {
            std::uint32_t mCells = 0;
            std::uint32_t mImages = 0;
            bool mArriving = false;

            bool operator==(const Unstood& other) const = default;
        };
        const Unstood& getUnstood() const { return mUnstood; }

        double getStalledMs() const { return mStalledMs; }
        std::uint32_t getWarmup() const { return mWarmup; }
        std::uint32_t getWarmedPaused() const { return mWarmedPaused; }

    private:
        std::uint32_t mWarmup = 0;
        std::uint32_t mPauseLimit = 0;
        bool mPlayed = false;

        std::uint32_t mSeen = 0;
        bool mWhole = false;
        std::uint32_t mStoodAt = 0;
        std::uint32_t mWaited = 0;
        Unstood mUnstood;
        double mStalledMs = 0.0;
        std::uint32_t mWarmedRan = 0;
        std::uint32_t mWarmedPaused = 0;
        std::optional<std::uint32_t> mMeasuredFrom;

        Rtx::FrameSpend mPendingSpend;
        std::uint32_t mPendingArrived = 0;
        std::optional<bool> mPendingCrossing;
        const void* mLastCell = nullptr;
    };

    /// Which of what a whole frame asks held a wait, as a stalled stop's failure says it: "5 cells short, 12 images
    /// still being made, content still arriving", each part only where it held.
    std::string describeUnstood(const MeasureWindow::Unstood& unstood);
}
