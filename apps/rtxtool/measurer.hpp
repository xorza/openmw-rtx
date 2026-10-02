#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <components/rtx/common/scratch.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/renderer/renderer.hpp>

#include "instruments/cardwatch.hpp"
#include "instruments/frametimes.hpp"
#include "instruments/scenedigest.hpp"
#include "model/benchrecord.hpp"
#include "model/measurewindow.hpp"

namespace MWRender
{
    struct FrameContext;
    struct FrameReport;
}

namespace RtxTool
{
    class RunRecord;
    class StopWriter;
    struct SessionRequest;
    struct Stop;

    /// Counts a stop's frames and measures them, straight into the place the stop comes to: the
    /// frame series, what the device answered for each frame, the crossings and the arrivals, the
    /// card's clock and who else held it, the hashes and the film's pictures.
    ///
    /// **Holds the run's instruments**, the card watch, perf's fifo and the scene digester, because
    /// each is a thread or a history that outlives a stop.
    class Measurer
    {
    public:
        /// What one frame came to for the stop.
        enum class Verdict
        {
            Going,
            Ended,

            /// The stop cannot go on, `getFailure` says why: a film missing a frame is a film cut
            /// short.
            Failed,
        };

        /// Reserves every series at the longest stop of `request`, so no measured frame grows one,
        /// and starts watching the card; `record` is what the run is written into.
        Measurer(const SessionRequest& request, RunRecord& record);

        /// Starts counting `stop`, with the player where the stager put them.
        void begin(const Stop& stop);

        /// Frames traced since the stop began, those ahead of the measurement included: what the
        /// trace's sampler and the upscaler's jitter are walked by and what the hashes number their
        /// rows by.
        std::uint32_t getSeen() const { return mProgress.mWindow.getSeen(); }

        /// Which of the stop's measured frames the frame about to be drawn is, counted from nought,
        /// or nothing while it is a frame ahead of them — until a frame drew the whole world, and
        /// the world has run, unpaused, through the warm-up the stop asked for after it. What a
        /// route flies over, a take's track is posed by and a reference is averaged over.
        std::optional<std::uint32_t> getMeasuredIndex() const { return mProgress.mWindow.getMeasuredIndex(); }

        /// Takes one traced frame of `stop`, and with it whatever the device answered for an earlier
        /// one. `Ended` once the stop has measured its length or its route has `arrived`.
        Verdict frame(
            const Stop& stop, const MWRender::FrameContext& context, const MWRender::FrameReport& report, bool arrived);

        /// Closes `stop` on the frame `report` is of, its last measured one: waits out the frames
        /// still on the queue, writes what the stop asked through `writer`, and gives back the place
        /// it came to, `travelled` of its route flown.
        BenchPlace finish(const Stop& stop, const MWRender::FrameContext& context, const MWRender::FrameReport& report,
            float travelled, StopWriter& writer);

        /// Why the stop cannot go on, or empty.
        const std::string& getFailure() const { return mFailure; }

    private:
        /// Takes in what the device answered for one frame of `stop`, under the number of the frame
        /// it answers for: the figures a frame has only once the device is done with it, and its
        /// picture. Nothing for a frame ahead of the measurement.
        void answered(const Stop& stop, const Rtx::FrameResult& finished, const Rtx::FrameExtents& extents);

        /// Hashes a frame's picture into the record, and writes it where the request asked for the
        /// pictures themselves, at `extents`.
        void keepPicture(const Rtx::FrameResult& finished, const Rtx::FrameExtents& extents);

        /// Writes a film's frame as the numbered picture it is, where `finished` is one.
        void writeFilmFrame(const Stop& stop, const Rtx::FrameResult& finished, const Rtx::FrameExtents& extents);

        /// What one stop has come to so far. `restart` rather than an assignment from a default,
        /// because the series are reserved once for the longest stop of the run.
        struct Progress
        {
            /// Which frames are measured, and which span each closes.
            MeasureWindow mWindow;

            /// The backend's number of the first measured frame, so a result that comes back once
            /// the measurement began can say whether the frame it answers for was measured.
            std::uint64_t mFirstMeasured = 0;

            /// How long the measured frames took between them, and what they wrote that was not
            /// finite.
            double mWallMs = 0.0;
            Rtx::NotFinite mNotFinite;

            /// Which frame of the film each frame in flight is, by the backend's number: a picture
            /// comes back a frame or two after the frame it was traced as, and is numbered by that.
            struct FilmFrame
            {
                std::uint64_t mFrame = 0;
                std::uint32_t mNumber = 0;
            };
            std::array<FilmFrame, 4> mFilmFrames{};
            std::size_t mFilmPending = 0;

            /// How many measured frames the world stood paused on, and what paused it: noted on the
            /// first paused frame ahead of the measurement for its note, and again on the first
            /// paused measured frame for the failure, so each names its own cause —
            /// `Stager::describePause`. Twice at most and never per frame, so a stop paused
            /// throughout allocates two strings.
            std::uint32_t mPausedFrames = 0;
            std::string mPausedBy;

            /// The cell the last measured frame was drawn in, so a change of it is a boundary
            /// crossed. Compared as an address and never read, which is all an identity needs.
            const void* mCell = nullptr;

            FrameSamples mSamples;

            GpuBreakdown mGpu;
            HoldTimes mHold;

            /// What the stop comes to, as it is measured: the crossings, the arrivals, the overlap and
            /// how much of the last frame hit something go straight in, and the rest at `finish`.
            BenchPlace mPlace;

            /// Empties it for the next stop, keeping the room the frame series and the zones' rows
            /// grew — what the longest stop of a run reserves.
            ///
            /// **Every field not named is reset by being unnamed**, so a field added to the struct is
            /// reset here whether or not its author remembered to.
            void restart() { Rtx::reuseKeeping(*this, &Progress::mSamples, &Progress::mGpu); }
        };

        const SessionRequest& mRequest;
        RunRecord& mRecord;

        /// perf's control fifo, held for the whole run so every stop brackets its own frames.
        PerfControl mProfiling;

        /// Whether the window before the first place was said, which is said once.
        bool mBeforeSaid = false;

        /// The card, watched from the session's start: its clock across each stop's measured
        /// frames, and who held it through every window of the run. Held rather than made per
        /// stop, because what it owns is a thread.
        CardWatch mCardWatch;

        /// What a hashed frame's scene columns come from, kept so a frame pays for what moved.
        SceneDigester mDigester;

        Progress mProgress;
        std::string mFailure;
    };
}
