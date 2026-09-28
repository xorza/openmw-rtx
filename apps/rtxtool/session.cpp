#include "session.hpp"

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/statemanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwrender/rtx/rtxrenderer.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/debug/debuglog.hpp>

namespace RtxTool
{
    Session::Session(SessionRequest request)
        : mRequest(std::move(request))
        , mInstalled{ .mSetup = mRequest.mSetup, .mRun = *this }
        , mHome(mRequest.mKeys, mRequest.mHomePictures)
        , mMeasurer(mRequest, mRecord)
    {
        if (!mRequest.mAgainst.empty())
            mRecord.readReference(mRequest.mAgainst);

        mRecord.reserve(mRequest.mStops.size());

        if (mRequest.mStops.empty())
            mDone = true;
    }

    std::unique_ptr<MWRender::Renderer> Session::createRenderer(const MWRender::RendererSpec& spec)
    {
        return std::make_unique<MWRender::RtxRenderer>(spec, &mInstalled);
    }

    std::optional<float> Session::getFrameStep() const
    {
        return mRequest.mSetup.mStep;
    }

    SessionResult Session::describe() const
    {
        return mRecord.describe(mNote.getLeft());
    }

    void Session::abandon(const std::string_view why)
    {
        Log(Debug::Error) << "Ray tracing session: " << why;
        mRecord.fail();
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
        const Stop& stop = currentStop();

        if (const std::optional<std::string> refused = mStager.stage(stop, mRequest))
        {
            abandon(*refused);
            return;
        }

        mCamera.begin(stop);
        mNote.begin(stop);
        mMeasurer.begin(stop);

        mStarted = true;

        const float step = mRequest.mSetup.getWorldStep();
        Log(Debug::Info) << "Ray tracing session: stop " << (mAt + 1) << " of " << mRequest.mStops.size() << ", "
                         << (stop.mName.empty() ? "unnamed" : stop.mName) << " — "
                         << stop.mSchedule.mSpec.getWarmup(step) << " frames warming up then "
                         << (stop.mSchedule.mSpec.mRun.isUntilClosed()
                                    ? std::string("a window until it is closed")
                                    : std::to_string(stop.mSchedule.mSpec.getMeasured(step)) + " measured");
    }

    std::optional<std::uint32_t> Session::getSampleFrame() const
    {
        if (mDone || !mStarted)
            return std::nullopt;

        return mMeasurer.getSeen();
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

        // **A game that has ended cannot be flown any further, and a session that waited for one
        // would wait for ever.** Every stop after this one is unreachable, so the run says what
        // happened and stops rather than drawing the same frame until somebody kills it. What ends
        // a game here is the player dying, which the stager turns god mode on to prevent — this is
        // for whatever else might.
        if (MWBase::Environment::get().getStateManager()->getState() == MWBase::StateManager::State_Ended)
        {
            abandon(std::format("the game ended during stop {} of {}, so no place after it can be reached", mAt + 1,
                mRequest.mStops.size()));
            return;
        }

        if (!isPlaying())
            return;

        if (!mRequest.mPlayed)
            Stager::closeMenus();

        if (!mStarted)
        {
            beginStop();
            return;
        }

        // **The reset stands until a frame has been counted, and the one the stager issued is not
        // enough.** A stop opens with frames nobody counts: the first trace after the teleport has
        // no predecessor to be timed against, so `frame` is never reached for it and the count
        // stays at nought. The exposure adapts on every one of them all the same, and how many
        // there are is a question about how long the world took to load rather than one the
        // schedule answers — so two runs began counting from two exposures and drew the first
        // thirty frames differently, with the same scene behind them. Reissued here, the last reset
        // lands on the frame that becomes the first counted one, whatever went before it.
        //
        // **Both calls, and neither is the other's spare.** The stager resets because a teleport
        // is a discontinuity and the frames it opens with are drawn on a screen. This resets
        // because those frames are not measured, and a measured run may not depend on them.
        if (mMeasurer.getSeen() == 0)
            Stager::forgetHistory();

        mHome.listen();
        if (const SkyPress press = mSkyKeys.listen(); press.mSteps != 0)
            mCamera.turnSkyBy(currentStop(), press.mSteps, press.mAtOnce);
        mCamera.step(currentStop(), mMeasurer.getMeasuredIndex(), mRequest.mSetup.getWorldStep());

        // **After the camera has stepped and on every frame, warm-up included.** `CameraDriver::aim`
        // says why once is not enough; the warm-up frames stand at the route's start.
        mCamera.aim(currentStop());
    }

    bool Session::holdsGameClock() const
    {
        return !mDone && mStarted && currentStop().mSchedule.mTrack.has_value();
    }

    void Session::frame(const MWRender::FrameContext& context, const MWRender::FrameReport& report)
    {
        if (mDone || !mStarted)
            return;

        // **The frame just drawn, which is the one on the screen**: the camera the player turned
        // during the frame's own update, the eye the route flew and the sky the turn crossed, and
        // the air the renderer stepped. Taken before the frame, the note would be a camera one update
        // behind the picture. The last one taken is what `RunRecord::describe` publishes.
        mNote.take(report.mAir);
        mHome.answer(mNote.getLeft(), report, context.mRenderer.getBackend().getExtents());

        switch (mMeasurer.frame(currentStop(), context, report, mCamera.hasArrived()))
        {
            case Measurer::Verdict::Going:
                return;
            case Measurer::Verdict::Failed:
                abandon(mMeasurer.getFailure());
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

        mRecord.add(
            mMeasurer.finish(stop, context, report, route.has_value() ? mCamera.getTravelled(*route) : 1.0f, mWriter));

        if (!mMeasurer.getFailure().empty())
        {
            abandon(mMeasurer.getFailure());
            return;
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
        mRecord.finish(mRequest);

        // **The way the quit key ends a session, and not `exit`.** A run that tore the process down
        // where it stood would leave the save, the log and the device wherever they happened to be,
        // and the next thing anyone would debug is the session.
        if (mRequest.mQuitAtEnd)
            MWBase::Environment::get().getStateManager()->requestQuit();
    }
}
