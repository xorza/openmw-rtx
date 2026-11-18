#include "session.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/statemanager.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwrender/rtx/rtxrenderer.hpp>
#include <apps/openmw/mwworld/datetimemanager.hpp>
#include <apps/openmw/mwworld/globals.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <apps/rtxtool/instruments/reportfile.hpp>
#include <apps/rtxtool/instruments/resourcecaches.hpp>
#include <apps/rtxtool/model/finding.hpp>
#include <apps/rtxtool/model/heapdescription.hpp>
#include <apps/rtxtool/model/stopdescription.hpp>
#include <components/crashcatcher/crash.hpp>
#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/result.hpp>
#include <components/platform/heap.hpp>
#include <components/platform/process.hpp>
#include <components/resource/cachebudget.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/rtx/common/mebibytes.hpp>

namespace RtxTool
{
    Session::Session(SessionRequest request)
        : mRequest(std::move(request))
        , mInstalled{ .mSetup = mRequest.rendererSetup(), .mRun = *this }
        , mHome(mRequest.mKeys, mRequest.mHomePictures)
        , mMeasurer(mRequest, mRecord)
    {
        if (!mRequest.mAgainst.empty())
            mRecord.readReference(mRequest.mAgainst);

        mRecord.reserve(mRequest.mStops.size());
        mRecord.begin(mRequest);

        if (mRequest.mStops.empty() && !mRequest.mPlanWhereTheWorldStands)
            mDone = true;
        if (mRequest.mWatching.has_value())
            mWatch.emplace(worldStep(mRequest.mStep), mRequest.mWatching->mPaced, mRequest.mWatching->mExpected);
    }

    Session::~Session()
    {
        if (mListening)
            Debug::setLogListener({});
    }

    std::unique_ptr<MWRender::Renderer> Session::createRenderer(const MWRender::RendererSpec& spec)
    {
        // The engine names the renderer it chose itself, and it chose none here.
        Crash::annotate("renderer", MWRender::nameOf(MWRender::RendererKind::RayTraced));
        auto renderer = std::make_unique<MWRender::RtxRenderer>(spec, &mInstalled);
        // **Before the first frame**, once `auto` has come to an upscaler: a stop the upscaler cannot
        // trace would otherwise break the reconstruction's contract on its first frame.
        if (const std::optional<std::string> refused = mRequest.refusalUnder(renderer->getUpscalerInput()))
            throw std::runtime_error("the run cannot be traced: " + *refused);
        // And the stops' lengths, where one is what the run's frames hold, at the extents the renderer made.
        mRequest.settleUnder(renderer->getExtents());
        mTraced = renderer->getExtents();
        mRecord.noteTraced(renderer->getExtents());
        mMeasurer.reserve();
        mMeasurer.watchCard(renderer->getCardAddress());
        return renderer;
    }

    std::optional<float> Session::getFrameStep() const
    {
        return mRequest.mStep;
    }

    std::optional<std::filesystem::path> Session::getSavesFolder() const
    {
        if (mRequest.mSaves.empty())
            return std::nullopt;
        return mRequest.mSaves;
    }

    void Session::endedEarly()
    {
        if (mDone || !mRequest.mQuitAtEnd)
            return;

        // The engine has stopped, so nothing is asked of it: the record is abandoned as far as the
        // run reached, which writes what was measured and fails the run.
        Log(Debug::Error) << "Ray tracing session: the game ended during stop " << (mAt + 1) << " of "
                          << mRequest.mStops.size() << ", before the run's last";
        noteFindings();
        mRecord.abandon(mRequest);
        mDone = true;
    }

    SessionResult Session::conclude()
    {
        return mRecord.conclude(mNote.getLeft());
    }

    bool Session::watchesWindow() const
    {
        return mWatch.has_value() && !mRequest.mSetup.mHeadless;
    }

    std::string_view Session::describeTitle()
    {
        const std::string_view note = mNote.describeTitle(mUpscaler);
        if (!mWatch.has_value() || mDone || !mStarted)
            return note;

        const auto [end, length] = std::format_to_n(mTitle.data(), static_cast<std::ptrdiff_t>(mTitle.size()),
            "{} - {} findings - {}", currentStop().mName, mWatch->getTable().getFindings().size(), note);
        return std::string_view(mTitle.data(), std::min(static_cast<std::size_t>(length), mTitle.size()));
    }

    void Session::writeSoFar()
    {
        const SessionRequest::Watching& watching = *mRequest.mWatching;
        mWatch->drainLog();
        std::string text = describeFindings(mWatch->getTable()) + mWatch->getRestReuse().describe()
            + mWatch->getLongest().describe();
        text
            += std::format("\nflown through {}, stop {} of {}\n", currentStop().mName, mAt + 1, mRequest.mStops.size());
        if (const Result<void, std::string> written = writeReport(watching.mFindings, text); !written.isOk())
            Log(Debug::Warning) << "Ray tracing session: " << written.error();
        mRecord.save(mRequest);
    }

    void Session::noteFindings()
    {
        if (!mWatch.has_value())
            return;
        mWatch->drainLog();
        const FindingTable& table = mWatch->getTable();
        mRecord.note(describeFindings(table) + mWatch->getRestReuse().describe());
        if (table.statusUnder(mRequest.mWatching->mFailOn) != ExitStatus::Passed)
            mRecord.fail();
    }

    void Session::abandon(const std::string_view why)
    {
        Log(Debug::Error) << "Ray tracing session: " << why;
        noteFindings();
        mRecord.abandon(mRequest);
        mDone = true;
        MWBase::Environment::get().getStateManager()->requestQuit();
    }

    bool Session::isPlaying() const
    {
        if (MWBase::Environment::get().getStateManager()->getState() != MWBase::StateManager::State_Running)
            return false;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        return world != nullptr && !world->getPlayerPtr().isEmpty();
    }

    void Session::beginStop()
    {
        // Before the first stop is staged, because staging moves the clock: the game's own speed is
        // what the world's `timescale` stood at when the session began.
        if (!mOwnTimeScale.has_value())
        {
            mOwnTimeScale = MWBase::Environment::get().getWorld()->getGlobalFloat(MWWorld::Globals::sTimeScale);
            if (!(*mOwnTimeScale > 0.0f))
            {
                abandon(
                    std::format("the world's timescale is {}, and a stopped clock has no speed of its own to "
                                "cross a sky or run a film's clock at",
                        *mOwnTimeScale));
                return;
            }
        }

        // **The stops a tour plans where the world stands**, once, the timescale read: none ends the run, as a
        // tour's `--plan` does once the plan is printed.
        if (mRequest.mPlanWhereTheWorldStands)
        {
            mRequest.mStops = std::exchange(mRequest.mPlanWhereTheWorldStands, {})(*mOwnTimeScale);
            // Nothing to fly ends the run here: a plan asked for was printed, and an empty content fails at the
            // run's end as any run that measured nothing does (`SessionRequest::mPlansOnly`).
            if (mRequest.mStops.empty())
            {
                mDone = true;
                MWBase::Environment::get().getStateManager()->requestQuit();
                return;
            }
            if (mTraced.has_value())
                mRequest.settleUnder(*mTraced);
            mRecord.begin(mRequest);
            mRecord.reserve(mRequest.mStops.size());
            mMeasurer.reserve();

            // **The log heard from where the world stands**, in place of the debug window's listener, which a run
            // nobody plays never shows: what the start said before is the start's and stands at no place.
            if (mWatch.has_value())
            {
                Debug::setLogListener([this](const Debug::Level level, std::string_view, const std::string_view text) {
                    mWatch->getLog().hear(level, text);
                });
                mListening = true;
            }
        }

        const Stop& stop = currentStop();

        // **A place the content does not hold is passed over, and the run goes on**: under `Morrowind.esm` alone a
        // run of every view ended at Mournhold and drew none of the seven after it. Whether the run fails for it is
        // the stop's (`WhenAbsent`). A flight that goes on from the stop before is staged by nothing: its world,
        // its sky and its camera are where that stop left them.
        // A stop that failed while its world arrived left it held, scripts off among it: let go of before the next stop
        // begins, which would read the scripts as off and never run them again.
        if (mHold.has_value())
        {
            Staging::releaseHold(*mHold);
            mHold.reset();
        }
        if (!stop.mSchedule.mContinues)
        {
            const Result<Staging::Hold, std::string> staged = Staging::stage(stop, mRequest);
            if (!staged.isOk())
            {
                Log(Debug::Warning) << "Ray tracing session: stop " << (mAt + 1) << " of " << mRequest.mStops.size()
                                    << " passed over: " << staged.error();
                mRecord.skip(stop, staged.error());
                advance();
                return;
            }
            mHold = staged.value();
        }

        // The world's own clock at the stop's pace, which the engine then advances a frame's step at a time, as a
        // player's game does under a script's `timescale`.
        if (stop.mSky.mClockTimes.has_value())
            MWBase::Environment::get().getWorld()->setGlobalFloat(
                MWWorld::Globals::sTimeScale, *mOwnTimeScale * *stop.mSky.mClockTimes);

        mCamera.begin(stop, *mOwnTimeScale);
        mNote.begin(stop);
        mMeasurer.begin(stop);
        if (mWatch.has_value())
            mWatch->begin(stop);

        mStarted = true;

        const float step = worldStep(mRequest.mStep);
        Log(Debug::Info) << "Ray tracing session: stop " << (mAt + 1) << " of " << mRequest.mStops.size() << ", "
                         << (stop.mName.empty() ? "unnamed" : stop.mName) << " — the world standing whole, "
                         << stop.mSchedule.mSpec.getWarmup(step) << " frames warming up, then "
                         << (stop.mSchedule.mSpec.mRun.isUntilClosed()
                                    ? std::string("a window until it is closed")
                                    : std::to_string(stop.mSchedule.mSpec.getMeasured(step)) + " measured");
    }

    std::optional<std::uint32_t> Session::getSampleFrame() const
    {
        if (mDone || !mStarted)
            return std::nullopt;

        if (mProbe.has_value())
            return mProbe;

        // The wait draws the stop's first sample, which its histories keep nothing of (`beforeFrame`).
        return currentStop().mSchedule.mSampleOffset + mMeasurer.getSinceWhole().value_or(0);
    }

    std::uint32_t Session::getAccumulated() const
    {
        if (mDone || !mStarted)
            return 0;

        const std::optional<std::uint32_t> measured = mMeasurer.getMeasuredIndex();
        if (currentStop().mSchedule.mAccumulate == 0 || !measured.has_value())
            return 0;

        // **Counted from the first measured frame**, because the warm-up is the world arriving and
        // the card coming off its idle clock. Averaging those in would put a picture of a
        // half-built cell into the reference.
        return *measured + 1;
    }

    std::optional<Rtx::ReconstructionRequest> Session::getReconstruction() const
    {
        if (mDone || !mStarted)
            return std::nullopt;

        return currentStop().mSchedule.mReconstruction;
    }

    std::optional<Rtx::ExposureRule> Session::getExposure() const
    {
        if (mDone || !mStarted)
            return std::nullopt;

        return currentStop().mSchedule.mExposure;
    }

    std::optional<Rtx::Upscale> Session::getUpscale() const
    {
        if (mDone || !mStarted)
            return std::nullopt;

        return currentStop().mSchedule.mUpscale;
    }

    bool Session::wantsSecondWalk() const
    {
        return !mDone && mStarted && currentStop().mActions.walksTwice();
    }

    bool Session::wantsFrameCopy() const
    {
        if (mDone || !mStarted)
            return false;

        const Actions& actions = currentStop().mActions;
        return actions.mHash || actions.mFilm.has_value() || mHome.wantsPicture();
    }

    std::optional<Rtx::AirClock> Session::getHeldAir() const
    {
        // **Until the first counted frame, for the reason the history is forgotten until then**
        // (`beforeFrame`): how many frames a stop opens with is how long the world took to load,
        // and an air that ran through them stood somewhere else in every run.
        if (mDone || !mStarted || mMeasurer.getSeen() != 0)
            return std::nullopt;

        return currentStop().mSky.mAir;
    }

    void Session::beforeFrame()
    {
        if (mDone)
            return;

        // A game that has ended cannot be flown any further, and a session that waited for one would wait for ever:
        // every stop after this one is unreachable, so the run says so and stops. The player dying is what ends a
        // game here, which the stager turns god mode on to prevent; this is for whatever else might.
        if (MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_Ended)
        {
            abandon(std::format("the game ended during stop {} of {}, so no place after it can be reached", mAt + 1,
                mRequest.mStops.size()));
            return;
        }

        if (!isPlaying())
            return;

        if (!mRequest.mPlayed)
            Staging::closeMenus();

        // A stop that throws as it begins is the run's failure, said once: `Engine::frame` catches what leaves here
        // and goes on, so the stop would begin again on every frame after (with `--perf-control` and nothing reading
        // it, thirty seconds a frame, for ever).
        if (!mStarted)
        {
            try
            {
                beginStop();
            }
            catch (const std::exception& failure)
            {
                abandon(
                    std::format("stop {} of {} could not begin: {}", mAt + 1, mRequest.mStops.size(), failure.what()));
            }
            return;
        }

        // **A stop's own time begins on the frame after its world stood whole** (`MeasureWindow::getSinceWhole`): its
        // clock, its histories and its samples, so how many frames the world took to arrive moves nothing the stop
        // shows. Under the Repack the armed deck's world stood whole on its frame 182 in one run of a binary and 183
        // or 185 in others; counted from the stop's first frame, each run sampled its measured frames at another
        // place in the sequence and moved its water a frame further.
        //
        // The histories are reset on every frame of the wait and on the first after it, where the last reset lands:
        // the exposure adapts and the denoiser accumulates on every frame, and a run may not depend on frames that
        // only the world's arrival counted. Both this and the stager's reset, and neither is the other's spare: the
        // stager resets because a teleport is a discontinuity and its frames are drawn on a screen; this because the
        // wait's frames are not the stop's. A flight that goes on from the stop before keeps all three as they stand.
        const std::optional<std::uint32_t> sinceWhole = mMeasurer.getSinceWhole();
        if (sinceWhole.value_or(0) == 0 && !currentStop().mSchedule.mContinues)
            Staging::forgetHistory();

        // **The clock runs from there** (`Staging::stage` holds it from the move), and never where the stop is frozen:
        // a frozen stop is a reference, so a frame traced many times is the same frame and an accumulated picture
        // converges on the integral rather than on the animation. Set ahead of the world's update, it moves the next
        // frame's step: the engine has taken this one's.
        MWWorld::DateTimeManager& clock = *MWBase::Environment::get().getWorld()->getTimeManager();
        const bool runs = !currentStop().mSchedule.mFrozen && sinceWhole.has_value();
        if ((clock.getSimulationTimeScale() > 0.0f) != runs)
            clock.setSimulationTimeScale(runs ? 1.0f : 0.0f);
        if (sinceWhole.has_value() && mHold.has_value())
        {
            Staging::releaseHold(*mHold);
            mHold.reset();
        }

        // Where somebody plays the run, and nowhere else: a key pressed in a measured window would move what the
        // measurement records nothing of. And not while a menu or the console has the keys: the three read the
        // keyboard's state and not the game's input, so text typed in the console turned the sky.
        // A tour's window marks where its watcher looks, and its keys move nothing else.
        if (watchesWindow())
        {
            const bool asked = mHome.wantsPicture();
            mHome.listen();
            if (!asked && mHome.wantsPicture())
                mWatch->mark(mNote);
        }
        if (mRequest.mPlayed && !MWBase::Environment::get().getWindowManager()->isGuiMode())
        {
            mHome.listen();
            mMemoryKey.listen();
            if (const SkyPress press = mSkyKeys.listen(); press.mSteps != 0)
                mCamera.turnSkyBy(currentStop(), press.mSteps, press.mAtOnce);
        }
        mCamera.step(currentStop(), mMeasurer.getMeasuredIndex(), worldStep(mRequest.mStep));

        // **After the camera has stepped and on every frame, warm-up included.** `CameraDriver::aim`
        // says why once is not enough; the warm-up frames stand at the route's start.
        mCamera.aim(currentStop());
    }

    bool Session::holdsGameClock() const
    {
        // A track's clock is the world's on the measured frames (`CameraDriver::step`), and held ahead of them, so a
        // take begins at its hour however long the world took to stand.
        return !mDone && mStarted && currentStop().mSchedule.mTrack.has_value()
            && !mMeasurer.getMeasuredIndex().has_value();
    }

    void Session::frame(const MWRender::FrameContext& context, const MWRender::FrameReport& report)
    {
        mUpscaler = describeUpscaler(context.mBackend.getProfile());
        if (mDone || !mStarted)
            return;

        // The frame just drawn, which is the one on the screen: the camera the player turned during the frame's own
        // update, the eye the route flew, the sky the turn crossed and the air the renderer stepped; taken before
        // the frame, the note would be a camera one update behind the picture. The last one taken is what
        // `RunRecord::describe` publishes. Every frame only where somebody plays the run, which is where the title
        // shows the note and the Home key reads it: work here lands in the next frame's time, so a measured run
        // takes the note once, at each stop's end.
        if (mRequest.mPlayed)
        {
            mNote.take(report.mAir);
            mHome.answer(mNote.getLeft(), report, context.mBackend.getExtents());
            mMemoryKey.answer(context);
        }
        else if (watchesWindow())
        {
            mNote.take(report.mAir);
            mHome.answer(mNote.getLeft(), report, context.mBackend.getExtents());
        }

        if (mProbe.has_value())
        {
            mMeasurer.probe(currentStop(), context.mBackend, report.mFrame);
            mProbe.reset();
            advance();
            return;
        }

        // The watch after the measurer, which closes the span the frame before worked in: a hitch is read there.
        const Measurer::Verdict verdict = mMeasurer.frame(currentStop(), context, report, mCamera.hasArrived());
        if (mWatch.has_value())
            mWatch->frame(context, report, mNote, mMeasurer.getMeasuredSpan());

        switch (verdict)
        {
            case Measurer::Verdict::Going:
                return;
            case Measurer::Verdict::Failed:
                failStop(mMeasurer.getFailure());
                return;
            case Measurer::Verdict::Ended:
                endStop(context, report);
                return;
        }
    }

    void Session::endStop(const MWRender::FrameContext& context, const MWRender::FrameReport& report)
    {
        const Stop& stop = currentStop();
        const std::optional<Route>& route = stop.mSchedule.mRoute;

        if (!mRequest.mPlayed)
            mNote.take(report.mAir);

        mRecord.add(
            mMeasurer.finish(stop, context, report, route.has_value() ? mCamera.getTravelled(*route) : 1.0f, mWriter));
        if (mWatch.has_value())
        {
            mWatch->endLeg(stop, mRecord.getPlaces().back(), mRecord.takeFailedChecks(), mNote);
            writeSoFar();
        }

        if (!mMeasurer.getFailure().empty())
        {
            failStop(mMeasurer.getFailure());
            return;
        }

        // **A still stands one frame more, for its probe**, after its record is written, so the
        // probe is no frame of the stop's: nothing measures it and nothing writes its picture.
        mProbe = mMeasurer.probeSample(stop);
        if (mProbe.has_value())
            return;

        advance();
    }

    void Session::failStop(const std::string_view why)
    {
        if (mRequest.mWhenAStopFails == WhenAStopFails::EndsTheRun)
        {
            abandon(why);
            return;
        }

        // Said at the level of news and not of an error, which a run that hears the log would count a second time
        // beside the failure it records (`TourWatch::failLeg`).
        Log(Debug::Info) << "Ray tracing session: stop " << (mAt + 1) << " of " << mRequest.mStops.size()
                         << " failed, and the run goes on: " << why;
        if (mWatch.has_value())
        {
            mWatch->failLeg(currentStop(), why, mNote);
            writeSoFar();
        }
        mRecord.fail(currentStop(), why);
        advance();
    }

    void Session::advance()
    {
        // What the game's resource caches read so far, at every stop's end: a run cut short still says it.
        Resource::ResourceSystem& resources = *MWBase::Environment::get().getResourceSystem();
        const Resource::CacheBudget& budget = resources.getCacheBudget();
        Log(Debug::Info) << "Resource caches after stop " << (mAt + 1) << ": "
                         << describeResourceCaches(readResourceCaches(resources))
                         << std::format(
                                "; together {:.1f} MiB of a budget of {:.1f} MiB, {} looks for room finding all in use",
                                Rtx::mebibytes(budget.getHeld()), Rtx::mebibytes(budget.getBytes()),
                                budget.getStarvedLooks());

        // And what the process holds of the system's memory beside them, whose peak the report says.
        if (const std::optional<Platform::Process::AnonymousMemory> anonymous = Platform::Process::anonymousMemory();
            anonymous.has_value())
        {
            const std::optional<Platform::HeapHeld> heap = Platform::heapHeld();
            // What holds the private memory past the heap, where the system walks it (Windows): a driver's or a
            // library's own allocations show as the other regions.
            const std::optional<Platform::Process::PrivateMemoryHeld> held = Platform::Process::privateMemoryHeld();
            Log(Debug::Info) << std::format(
                "Host memory after stop {}: {:.1f} MiB anonymous resident, {:.1f} MiB "
                "swapped out{}{}",
                (mAt + 1), Rtx::mebibytes(anonymous->mResident), Rtx::mebibytes(anonymous->mSwapped),
                heap.has_value() ? "; " + describeHeap(*heap) : std::string(),
                held.has_value()
                    ? std::format("; private memory {:.1f} MiB in the heaps' regions, {:.1f} MiB in {} other "
                                  "regions, the largest {:.1f} MiB",
                          Rtx::mebibytes(held->mHeaps), Rtx::mebibytes(held->mOther), held->mOtherRegions,
                          Rtx::mebibytes(held->mLargestOther))
                    : std::string());
            mRecord.noteHostMemory(currentStop().mName, *anonymous, heap);
        }

        mStarted = false;
        ++mAt;

        if (mAt < mRequest.mStops.size())
            return;

        finish();
    }

    void Session::finish()
    {
        mDone = true;
        noteFindings();
        mRecord.finish(mRequest);

        // The way the quit key ends a session, and not `exit`: tearing the process down where it stood would leave
        // the save, the log and the device wherever they happened to be.
        if (mRequest.mQuitAtEnd)
            MWBase::Environment::get().getStateManager()->requestQuit();
    }
}
