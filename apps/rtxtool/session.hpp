#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include <apps/openmw/engine.hpp>
#include <apps/openmw/mwrender/rtx/framereport.hpp>
#include <apps/openmw/mwrender/rtx/rtxrun.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/runrecord.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/world/frameworld.hpp>

#include "cameradriver.hpp"
#include "homekey.hpp"
#include "measurer.hpp"
#include "memorykey.hpp"
#include "skykeys.hpp"
#include "stager.hpp"
#include "standingnote.hpp"
#include "stopwriter.hpp"
#include "tourwatch.hpp"

namespace MWRender
{
    class Renderer;
    struct RendererSpec;
}

namespace RtxTool
{
    /// Drives a run of the game and measures it: the game, because a staged world never pays for the whole-graph
    /// walk, the sweep or a cell arriving, which are what cost a frame. It reads the world through
    /// `MWBase::Environment`, is fed each frame by `MWRender::RtxRenderer` through the interface it implements, and
    /// ends the run through `StateManager::requestQuit` the way the player's quit key does.
    ///
    /// The sequence and nothing else: a stop is staged by `Staging`, flown by the `CameraDriver`, noted by the
    /// `StandingNote` and measured by the `Measurer`; this is the engine's host that says which runs when. It is
    /// built before the engine and makes the renderer with itself installed as `RtxSetup::mRun`; what it came to is
    /// read with `describe` once `Engine::go` has returned, where a run that ends its last stop and a window
    /// somebody closes both end, and only the first ever reaches `finish`.
    class Session final : public MWRender::RtxRun, public OMW::EngineHost
    {
    public:
        explicit Session(SessionRequest request);

        /// Pinned: its measurer holds the request and the record beside it, and its installed run
        /// answers to it, by reference.
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
        Session(Session&&) = delete;
        Session& operator=(Session&&) = delete;

        /// Gives the log back its listener, where the run heard it.
        ~Session() override;

        /// The renderer this tool exists to drive, made with the run: the engine starts no renderer
        /// of its own and never sees the run.
        std::unique_ptr<MWRender::Renderer> createRenderer(const MWRender::RendererSpec& spec) override;

        /// The run's own step, or the wall for a window somebody watches.
        std::optional<float> getFrameStep() const override;

        std::optional<std::uint32_t> getSampleFrame() const override;
        std::uint32_t getAccumulated() const override;
        std::optional<Rtx::ReconstructionRequest> getReconstruction() const override;
        std::optional<Rtx::ExposureRule> getExposure() const override;
        std::optional<Rtx::Upscale> getUpscale() const override;
        bool wantsSecondWalk() const override;
        bool wantsFrameCopy() const override;
        std::optional<Rtx::AirClock> getHeldAir() const override;

        /// Starts the stop that is due, or moves the running one a frame on. Does nothing until the
        /// game has a world to stand in.
        void beforeFrame() override;

        /// Through every frame of a take, whose track states the hour (`CameraDriver::step`): a clock left running
        /// would carry it past the track's, and the driver only ever runs the clock forward. A stop whose clock the
        /// world runs (`StopSky::mClockTimes`) is held only ahead of its measured frames.
        bool holdsGameClock() const override;

        /// None, in every verb: a run nobody plays has nobody to answer, and a box would stop the world as it stops
        /// a film's (a film flown past Seyda Neen's census office stood paused behind `CharGenJournalMessage`).
        bool showsScriptMessageBoxes() const override { return false; }

        std::optional<std::filesystem::path> getSavesFolder() const override;

        /// Measures the frame, and asks the game to quit once the last stop is done.
        void frame(const MWRender::FrameContext& context, const MWRender::FrameReport& report) override;

        /// The note, and for a tour the leg and the findings so far before it.
        std::string_view describeTitle() override;

        /// After `Engine::go` returns: a run meant to end on its own that ended before its last stop, a window
        /// closed or a script's quit, is abandoned there, so it fails, writes what it reached, and is compared,
        /// where it would otherwise exit nought and leave the last run's record standing.
        void endedEarly();

        /// What the run came to: the places, the report, the verdict, where the eye was left and the pictures it
        /// kept. Asked once, as the pictures are handed over.
        SessionResult conclude();

    private:
        /// Whether the game has a world with a player in it. Nothing happens before it does.
        bool isPlaying() const;

        /// Ends the run as a failure, saying why, and asks the game to quit. An exit status and not
        /// a throw, because a run that cannot go on has still measured whatever it reached.
        void abandon(std::string_view why);

        /// Whether a tour is watched in a window: its Home marks, and its note is taken every frame for it.
        bool watchesWindow() const;

        /// The record and the findings so far, written after every leg, so a tour that dies says how far it came.
        void writeSoFar();

        /// Puts the findings in the record, and fails it where they fail the run: before the record is written.
        void noteFindings();

        /// A stop that cannot go on: the run ends there, or the stop is recorded as failed and the next begins
        /// (`SessionRequest::mWhenAStopFails`).
        void failStop(std::string_view why);

        /// Stages the stop `mAt` names and starts its camera, its note and its count.
        void beginStop();

        /// Closes the stop, records it, and moves to the next one — or ends the run. `report` is
        /// the last measured frame's, which is the frame every writer describes.
        void endStop(const MWRender::FrameContext& context, const MWRender::FrameReport& report);

        /// Writes what the run was asked to write and ends it.
        void finish();

        const Stop& currentStop() const { return mRequest.mStops[mAt]; }

        /// Moves on to the next stop, or ends the run after the last.
        void advance();

        SessionRequest mRequest;

        /// The sample the current still's probe is traced with, while its one frame is due
        /// (`Measurer::probeSample`): the stop stands until it has come back.
        std::optional<std::uint32_t> mProbe;

        /// What the renderer is made with: the request's (`SessionRequest::rendererSetup`), and
        /// this as the run. After the request, which it is made from.
        const MWRender::RtxSetup mInstalled;

        /// The world's `timescale` when the first stop began: the game's own clock speed for the
        /// whole session (`CameraDriver::begin`).
        std::optional<float> mOwnTimeScale;

        /// The extents the renderer traces and shows at, once it is made: what the stops a run plans where the world
        /// stands are settled under (`SessionRequest::settleUnder`).
        /// What a tour meets, where the request watches for findings (`SessionRequest::mWatching`).
        std::optional<TourWatch> mWatch;

        /// The tour's title, which the leg's name and the note are written into.
        std::array<char, 160> mTitle{};

        /// Whether the log's listener is this run's (`TourWatch::getLog`).
        bool mListening = false;

        std::optional<Rtx::FrameExtents> mTraced;

        /// Which stop is running, and whether it has been started.
        std::size_t mAt = 0;
        bool mStarted = false;
        bool mDone = false;

        /// What the run has come to so far: the places, the report and the verdict. Its own type,
        /// because everything with something to say writes into all of it. Before the measurer,
        /// which writes into it.
        RunRecord mRecord;

        CameraDriver mCamera;
        StandingNote mNote;

        /// The upscaler the last frame passed through, as the title names it (`describeUpscaler`):
        /// read every frame, since a stop may trace at a mode of its own.
        std::string_view mUpscaler;
        HomeKey mHome;
        SkyKeys mSkyKeys;
        MemoryKey mMemoryKey;
        Measurer mMeasurer;
        StopWriter mWriter;
    };
}
