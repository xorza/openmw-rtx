#include "measurer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwrender/rtx/framereport.hpp>
#include <apps/openmw/mwrender/rtx/rtxrenderer.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <apps/openmw/mwworld/timestamp.hpp>
#include <components/crashcatcher/crash.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/environment/skylight.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/renderer/png.hpp>
#include <components/rtx/renderer/sceneuploader.hpp>

#include "film.hpp"
#include "instruments/framehashes.hpp"
#include "instruments/gpuclock.hpp"
#include "model/benchrun.hpp"
#include "model/benchspec.hpp"
#include "model/runrecord.hpp"
#include "stager.hpp"
#include "stopwriter.hpp"

namespace RtxTool
{
    namespace
    {
        /// **How long a stop waits on a paused world before it gives up, in seconds of world.** What
        /// the session can lift — a menu — lifts within a frame or two, the interface's script
        /// unpausing on the notice the close sends; what is still there after a hundred times that
        /// is a pause the session cannot lift, and a stop waiting on it would wait for ever.
        constexpr float sPauseSeconds = 2.0f;

        /// **How long a stop waits on a world that stopped arriving, in milliseconds of the wall.**
        /// A settled walk adopts a cell a frame and an unsettled one whenever the reader hands one
        /// over: the deck's whole band, four hundred and twenty frames of it, stood in seven seconds.
        /// Ten seconds with no cell is a reader that is stuck or a ring that leaves cells it never
        /// asks for, and not a slow one.
        constexpr double sStallMs = 10000.0;
    }

    Measurer::Measurer(const SessionRequest& request, RunRecord& record)
        : mRequest(request)
        , mRecord(record)
        , mProfiling(request.mPerfControl)
    {
        // Reserved once at the longest stop's length, so no measured frame grows a vector — a
        // benchmark that stops to reallocate is measuring its own allocator. A window that runs
        // until it is closed is not a benchmark, and room for its "length" is thirty gigabytes a
        // row: Linux promised that and never gave it, and Windows refuses it outright.
        std::uint32_t longest = 0;
        for (const Stop& stop : request.mStops)
            if (!stop.mSchedule.mSpec.mRun.isUntilClosed())
                longest = std::max(longest, stop.mSchedule.mSpec.getMeasured(worldStep(request.mSetup)));

        mProgress.mSamples.reserve(longest);
        mProgress.mGpu.reserve(longest);

        // **From here and not from the first frame**, because the window before the first stop
        // is the load, at the card's idle clock, where a desktop that is drawing shows plainest;
        // `CardWatch` says why.
        mCardWatch.watch();
    }

    void Measurer::begin(const Stop& stop)
    {
        // **First, because everything below writes into it.** A stop's progress is one object so
        // that a field added to it is reset here whether or not its author remembered to.
        mProgress.restart();

        mProgress.mWarmup = stop.mSchedule.mSpec.getWarmup(worldStep(mRequest.mSetup));
        mProgress.mCell = MWBase::Environment::get().getWorld()->getPlayerPtr().getCell();
        mProgress.mPlace.mView = stop.mName;
        mProgress.mPlace.mCell = stop.mStand.mCell;
        mProgress.mPlace.mNote = stop.mNote;
    }

    std::optional<std::uint32_t> Measurer::getMeasuredIndex() const
    {
        if (mProgress.mMeasuredFrom.has_value())
            return mProgress.mSeen - *mProgress.mMeasuredFrom;

        // The world stood whole and the warm-up has run its length since, so the frame about to
        // be drawn opens the measurement, which `frame` marks when it takes that frame.
        if (mProgress.mWhole && mProgress.mWarmedRan == mProgress.mWarmup)
            return 0u;

        return std::nullopt;
    }

    Measurer::Verdict Measurer::frame(const Stop& stop, const MWRender::FrameContext& context,
        const MWRender::FrameReport& report, const bool arrived)
    {
        Rtx::Renderer& renderer = context.mRenderer.getBackend();
        const double frameMs = report.mSpend.at(Rtx::Timing::Frame);
        const float step = worldStep(mRequest.mSetup);
        const std::uint32_t measured = stop.mSchedule.mSpec.getMeasured(step);

        if (!mProgress.mMeasuredFrom.has_value() && mProgress.mWhole && mProgress.mWarmedRan == mProgress.mWarmup)
        {
            mProgress.mMeasuredFrom = mProgress.mSeen;

            // Said where it happened, so a run that took longer to arrive than it was asked to
            // says why. Nothing for a world that stood whole on its first frame.
            if (mProgress.mWaited > 1)
                mRecord.note(std::format("{}: the world stood whole on its frame {}, and the warm-up ran {} after it\n",
                    stop.mName, mProgress.mWaited, mProgress.mWarmup));
            if (mProgress.mWarmedPaused > 0)
                mRecord.note(
                    std::format("{}: the world stood paused on {} frames ahead of the measurement, held by {}\n",
                        stop.mName, mProgress.mWarmedPaused, mProgress.mPausedBy));

            // **Sampled through the measured frames and not at their ends.** Two readings bound
            // nothing: the ends of a place agree to within a couple of per cent while the card
            // moves a fifth of its clock between them, and a leg that lost its clock then reads
            // like a leg that lost its speed.
            //
            // **What the window before answered is said once, ahead of the first place.** A
            // desktop that was drawing while the run loaded was caught in nearly every sample,
            // and every place's own line then reads against it.
            const CardShare before = mCardWatch.start();
            if (mRecord.empty() && before.mViewed)
                mRecord.note(std::format("before the first stop, {}\n", describeCard(before)));

            mProfiling.enable();

            // The backend's number of the first measured frame: what says of a result that comes
            // back later whether the frame it answers for was measured.
            mProgress.mFirstMeasured = report.mFrame;
        }

        // **Counted here, at the frame the run traced, and never at the frame the device answered
        // for it.** The count is what the trace's sampler and the upscaler's jitter are walked by,
        // what the hashes table numbers its rows by and what ends the stop; and a result comes back
        // one frame later or two, by whether the card had finished when the frame after asked —
        // so a count of results put two runs of one build at different points of the sequence,
        // and paired the scene of one frame with the picture of another. What the device answered
        // is taken in below, under the number of the frame it answers for.
        ++mProgress.mSeen;

        if (report.mResult.has_value())
            answered(stop, *report.mResult, renderer.getExtents());
        if (!mFailure.empty())
            return Verdict::Failed;

        // **This frame's wall time closes the span the frame before it worked in.**
        // `Timing::Frame` runs from the last frame's opening to this one's, so what it
        // holds is the game's update this frame arrived through and the renderer's work of the
        // frame before — the finish, the walk, the placement and the trace that ran after that
        // frame opened. Those are the rows kept beside it, and the meshes that work brought, so a
        // row's figures are the figures of the span it is read against and a worst frame's shares
        // are its own. What this frame does is kept for the frame after to close, and the last
        // measured frame's work is closed by nothing: it ran after the last span ended.
        Rtx::FrameSpend closed = mProgress.mPendingSpend;
        closed.at(Rtx::Timing::Frame) = frameMs;
        closed.at(Rtx::Timing::Update) = report.mSpend.at(Rtx::Timing::Update);
        closed.at(Rtx::Timing::Sleep) = report.mSpend.at(Rtx::Timing::Sleep);
        const std::uint32_t closedArrived = mProgress.mPendingArrived;
        mProgress.mPendingSpend = report.mSpend;
        mProgress.mPendingArrived = report.mArrivedMeshes;

        if (!mProgress.mMeasuredFrom.has_value())
        {
            if (!report.mPaused || mRequest.mPlayed)
            {
                if (mProgress.mWhole)
                {
                    ++mProgress.mWarmedRan;
                    return Verdict::Going;
                }

                // **A stop is measured from the frame after one that drew the whole world**, and
                // never by a count of frames from its first. A ring adopts one cell a frame, so a
                // reach of ten cells is some four hundred frames before its last cell stands, and
                // a warm-up of two seconds measured, and pictured, a third of the ground. The frame
                // that stood whole is no frame of the warm-up, which is what a stop asks for after it.
                //
                // **Waited on for as long as it comes nearer, and no longer**: a frame that left
                // fewer cells to stand than any before it is the world arriving, and the wait fails
                // where `sStallMs` of frames brought none. Never by a count of frames, because an
                // unsettled walk adopts a cell when the reader has one and not every frame.
                if (mProgress.mWaited++ == 0 || report.mCellsToStand < mProgress.mLeastToStand)
                {
                    mProgress.mLeastToStand = report.mCellsToStand;
                    mProgress.mStalledMs = 0.0;
                }
                else
                    mProgress.mStalledMs += frameMs;
                mProgress.mWhole = report.isWhole();

                // Not in a session somebody plays, which walks where it likes from its first frame
                // and brings cells in as it walks.
                if (!mProgress.mWhole && !mRequest.mPlayed && mProgress.mStalledMs > sStallMs)
                {
                    mFailure = std::format(
                        "the world of {} did not stand whole through {} frames: {} cells short, "
                        "and the last {:.1f} s brought none of them",
                        stop.mName, mProgress.mWaited, mProgress.mLeastToStand, mProgress.mStalledMs / 1000.0);
                    return Verdict::Failed;
                }

                return Verdict::Going;
            }

            if (mProgress.mWarmedPaused++ == 0)
                mProgress.mPausedBy = Stager::describePause();

            if (mProgress.mWarmedPaused > BenchSpan{ .mSeconds = sPauseSeconds }.getFrames(step))
            {
                mFailure = std::format("the world stood paused through {} frames ahead of {}, held by {}",
                    mProgress.mWarmedPaused, stop.mName, mProgress.mPausedBy);
                return Verdict::Failed;
            }

            return Verdict::Going;
        }

        // A window that runs until it is closed is looked at and not measured: nothing reads its
        // figures, and every series kept for it grew for as long as the window stood open.
        if (stop.mSchedule.mSpec.mRun.isUntilClosed())
            return Verdict::Going;

        mProgress.mSamples.add(closed);
        mProgress.mPlace.mArrivals.add(closedArrived, closed);
        mProgress.mWallMs += frameMs;

        // **A measured frame of a paused world is a frame of something else**, and nothing else in
        // a report says so: the figures of a world standing still look like any other place's. A
        // menu a script opened paused every run under M[FR]'s hotkey notice until the session
        // learnt to close one (`Stager::closeMenus`), and the console, a message box waiting for an
        // answer or a script's own tag pause it the same way. The frame's own flag, which is the
        // one it was drawn under. Not in a session somebody plays, whose pauses are theirs.
        if (report.mPaused && !mRequest.mPlayed && mProgress.mPausedFrames++ == 0)
            mProgress.mPausedBy = Stager::describePause();

        // **Counted here and not where the route moved**, because a crossing is a dropped frame and
        // this is where what it dropped is known. The move pulls the next ring in and that read
        // lands in the frame after it — which is the frame that arrives here standing in a cell it
        // was not drawn in last time, and the frame that paid for it.
        //
        // **The whole frame goes in as the read**, because the game gives no split: the ring
        // arrives on the loading threads, and what a crossing costs here is the frame that dropped.
        if (const void* cell = MWBase::Environment::get().getWorld()->getPlayerPtr().getCell();
            mProgress.mCell != nullptr && cell != mProgress.mCell)
        {
            mProgress.mPlace.mCrossings.add(report.mUpload == Rtx::SceneUpload::Kind::Rebuilt, frameMs);
            mProgress.mCell = cell;
        }

        const std::uint32_t drawn = mProgress.mSeen - *mProgress.mMeasuredFrom;

        // Numbered here, where the frame is traced, for `writeFilmFrame` to find when its picture
        // comes back.
        if (const std::optional<Actions::Film>& film = stop.mActions.mFilm; film.has_value())
        {
            Crash::contract(mProgress.mFilmPending < mProgress.mFilmFrames.size(),
                "more of a film's frames in flight than the ring holds");
            mProgress.mFilmFrames[mProgress.mFilmPending++]
                = Progress::FilmFrame{ .mFrame = report.mFrame, .mNumber = film->mFirst + drawn - 1 };
        }

        // The scene of this frame under the number the backend gave the frame, which is what the
        // picture finds its row by when it comes back. A frame ahead of the measurement has no row.
        if (stop.mActions.mHash)
            mRecord.getHashes().note(
                stop.mName, drawn, report.mFrame, mDigester.digest(context.mScene, &report.mConstants));

        return drawn < measured && !arrived ? Verdict::Going : Verdict::Ended;
    }

    void Measurer::answered(const Stop& stop, const Rtx::FrameResult& finished, const Rtx::FrameExtents& extents)
    {
        // A frame ahead of the measurement: its picture has no row and its figures are nobody's.
        if (!mProgress.mMeasuredFrom.has_value() || stop.mSchedule.mSpec.mRun.isUntilClosed()
            || finished.mFrame < mProgress.mFirstMeasured)
            return;

        mProgress.mPlace.mOverlap.add(finished.mInFlight);
        mProgress.mGpu.add(finished.mGpu.spans());
        mProgress.mNotFinite.add(finished.mNotFinite);

        // A frame that held, and no other: a run with no hold has no reading to summarise, and a
        // loop that left nothing behind is a frame `QueueHeld` counts as unheld.
        if (finished.mHeldMs > 0.0)
            mProgress.mHold.add(finished.mHeldMs);

        const double traced = static_cast<double>(extents.mRenderWidth) * extents.mRenderHeight;
        if (traced > 0.0)
            mProgress.mPlace.mHitPercent = static_cast<double>(finished.mHits) / traced * 100.0;

        if (stop.mActions.mHash)
            keepPicture(finished, extents);

        if (stop.mActions.mFilm.has_value())
            writeFilmFrame(stop, finished, extents);
    }

    void Measurer::writeFilmFrame(const Stop& stop, const Rtx::FrameResult& finished, const Rtx::FrameExtents& extents)
    {
        std::array<Progress::FilmFrame, 4>& pending = mProgress.mFilmFrames;
        const auto end = pending.begin() + static_cast<std::ptrdiff_t>(mProgress.mFilmPending);
        const auto found = std::find_if(
            pending.begin(), end, [&](const Progress::FilmFrame& one) { return one.mFrame == finished.mFrame; });
        Crash::contract(found != end, "a film's measured frame came back that `frame` never numbered");

        const std::uint32_t number = found->mNumber;
        std::copy(found + 1, end, found);
        --mProgress.mFilmPending;

        // **A film with a frame missing is a film cut short**: the encoder reads the sequence up to
        // its first gap, so a picture that did not come back ends the run rather than leaving one.
        if (finished.mPixels.empty())
        {
            mFailure = std::format("frame {} of the film came back without its picture", number);
            return;
        }

        const Misc::Result<void, std::string> written
            = Rtx::writePng(stop.mActions.mFilm->mDirectory / frameName(number), extents.mOutputWidth,
                extents.mOutputHeight, finished.mPixels);
        if (!written.isOk())
            mFailure = std::format("frame {} of the film: {}", number, written.error());
    }

    void Measurer::keepPicture(const Rtx::FrameResult& finished, const Rtx::FrameExtents& extents)
    {
        if (finished.mPixels.empty())
            return;

        const std::optional<FrameHashes::Pictured> row = mRecord.getHashes().picture(finished);
        if (!row.has_value() || mRequest.mPictures.empty())
            return;

        const Misc::Result<void, std::string> written
            = Rtx::writePng(mRequest.mPictures / std::format("{}-{}.png", row->mView, row->mFrame),
                extents.mOutputWidth, extents.mOutputHeight, finished.mPixels);
        if (!written.isOk() && mFailure.empty())
            mFailure = written.error();
    }

    BenchPlace Measurer::finish(const Stop& stop, const MWRender::FrameContext& context,
        const MWRender::FrameReport& report, const float travelled, StopWriter& writer)
    {
        Rtx::Renderer& renderer = context.mRenderer.getBackend();
        const float step = worldStep(mRequest.mSetup);

        mProfiling.disable();

        const CardReading card = mCardWatch.stop();
        BenchPlace& place = mProgress.mPlace;
        place.mClock = card.mClock;
        place.mCard = card.mShare;

        const Rtx::FrameExtents extents = renderer.getExtents();

        // The last frames' answers are still on the queue: waited out here, where a drain is a
        // stop's to pay and never a frame's, so every measured frame's figures and picture are in
        // the place they belong to.
        while (const std::optional<Rtx::FrameResult> finished = renderer.finishFrame())
            answered(stop, *finished, extents);

        // Drained, so a film frame still numbered is one whose picture will not come: the film
        // stops at that gap, and the run that made it may not end as though it had not.
        if (stop.mActions.mFilm.has_value() && mProgress.mFilmPending > 0 && mFailure.empty())
            mFailure = std::format("{} of the film's last frames never came back", mProgress.mFilmPending);

        if (mProgress.mPausedFrames > 0)
        {
            // The same guard over the measured frames, where a pause is no longer the world
            // arriving but the figures of a world standing still.
            const std::string why = std::format("the world stood paused on {} of {} measured frames of {}, held by {}",
                mProgress.mPausedFrames, stop.mSchedule.mSpec.getMeasured(step), stop.mName, mProgress.mPausedBy);
            Log(Debug::Error) << "Ray tracing session: " << why;
            mRecord.note(why + '\n');
            mRecord.fail();
        }

        // **A still is one frame traced again, and its depth and motion cannot move unless the
        // code under them did.** Asked of every hashed still that nothing jittered and nothing
        // flew. The build pins the float arithmetic the driver's second code could otherwise take
        // apart (`Rtx::pinFloatArithmetic`), so a frame where either moved is a swapped code
        // computing one of the operations left to the device — a division, a root, a
        // transcendental — otherwise, and the stop's frames are then two codes' and no reference.
        if (stop.mSchedule.mFrozen && !stop.mSchedule.mRoute.has_value() && stop.mActions.mHash
            && !report.mReconstruction.mJitter)
            if (const std::optional<std::uint32_t> moved = mRecord.getHashes().findStillMoved(stop.mName))
            {
                const std::string why = std::format(
                    "the driver's code changed during {}: depth or motion moved at frame {}", stop.mName, *moved);
                Log(Debug::Error) << "Ray tracing session: " << why;
                mRecord.note(why + '\n');
                mRecord.fail();
            }

        if (mRecord.empty())
        {
            // **Taken at the first stop, and bench's alone.** Only `bench` writes the header
            // (`--json`), and a bench's stops override none of the upscaling, the reconstruction
            // and the exposure, so the first stop's are the run's; a command whose stops override
            // them writes none. The upscaling is the frame's own answer — the pair the renderer
            // resolved this frame — and not the renderer's mode alone.
            BenchHeader& header = mRecord.getHeader();
            header.mExtents = extents;
            header.mUpscale = report.mReconstruction.mUpscale;
            header.mNoise = report.mReconstruction.mNoise;
            header.mLevelBias = report.mReconstruction.mLevelBias;
            header.mValidating = renderer.isValidating();
            header.mMeasured = stop.mSchedule.mSpec.getMeasured(step);
            header.mWarmup = stop.mSchedule.mSpec.getWarmup(step);
        }

        // Summarised ahead of the writer, whose checks read the zones, and kept for the place.
        const std::span<const GpuZone> zones = mProgress.mGpu.summariseZones();

        writer.write(context, report, stop.mActions,
            StopFacts{
                .mSamples = mProgress.mSamples,
                .mCrossings = place.mCrossings,
                .mStand = stop.mStand,
                .mOverlap = place.mOverlap,
                .mZones = zones,
                .mHold = mProgress.mHold,
                .mHoldAskedMs = mRequest.mSetup.mProfile.mStressOverlapMs,
                .mNotFinite = mProgress.mNotFinite,
            },
            mRecord);

        // **The hour and the sky of one moment, the stop's last frame**, so a stop the sky turned
        // over, or a film's take that crossed from one weather to another, is not reported at its
        // closing hour under its opening sky.
        const MWBase::World& world = *MWBase::Environment::get().getWorld();
        place.mHour = world.getTimeStamp().getHour();
        place.mWeather = Rtx::weatherName(static_cast<std::uint32_t>(world.getCurrentWeatherScriptId()));
        place.mFrames = mProgress.mSamples.size();
        place.mWallSeconds = mProgress.mWallMs / 1000.0;
        for (std::size_t at = 0; at < Rtx::sTimingCount; ++at)
            place.mRows[at] = summarise(mProgress.mSamples.mRows[at]);
        place.mTravelled = travelled;
        place.mScene = renderer.getSceneStats();
        place.mMemory = renderer.getMemoryReport();
        place.mContent = context.mRenderer.getContentMemory();
        place.mGpu.assign(zones.begin(), zones.end());

        return std::move(place);
    }
}
