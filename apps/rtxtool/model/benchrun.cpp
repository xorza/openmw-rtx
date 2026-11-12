#include "benchrun.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <apps/rtxtool/instruments/wholenumber.hpp>
#include <components/rtx/common/namedenum.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>

#include "blockfile.hpp"

namespace RtxTool
{
    namespace
    {
        /// One check: the word it is asked for by, and whether a stop is shaped to answer it.
        struct CheckRow
        {
            Check mCheck;
            std::string_view mName;

            /// A claim a stop is not shaped for answers something else, so it is left out rather
            /// than counted as a failure: a crossing count needs a route to cross anything with,
            /// and only the view says whether there is one.
            bool (*mCanAsk)(const Stop& stop, const Rtx::RenderProfile& profile);
        };

        constexpr bool always(const Stop&, const Rtx::RenderProfile&)
        {
            return true;
        }

        constexpr bool routed(const Stop& stop, const Rtx::RenderProfile&)
        {
            return stop.mSchedule.mRoute.has_value();
        }

        /// A route arrives at cells, and an arrival that rebuilds the scene, or places it twice in
        /// one frame, drains the ring.
        constexpr bool unrouted(const Stop& stop, const Rtx::RenderProfile&)
        {
            return !stop.mSchedule.mRoute.has_value();
        }

        /// **Asked of a stop that stands still.** A route leaves the camera wherever it flew to and
        /// `Stand` names only where it set off from, so the two legitimately differ by the whole
        /// length of the route.
        constexpr bool standingStill(const Stop& stop, const Rtx::RenderProfile&)
        {
            return stop.mStand.mEye.has_value() && !stop.mSchedule.mFreeCamera && !stop.mSchedule.mRoute.has_value();
        }

        /// A stop that names its weather and turns none: one that turns stands under each weather of
        /// the turn in its time.
        constexpr bool namesItsSky(const Stop& stop, const Rtx::RenderProfile&)
        {
            return stop.mSky.mWeather.has_value() && stop.mSky.mTurnThrough.empty();
        }

        /// A hold a run did not ask for cannot come out short.
        constexpr bool held(const Stop&, const Rtx::RenderProfile& profile)
        {
            return profile.mStressOverlapMs > 0.0;
        }

        /// Every check, the word it is asked for by, and when it may be asked.
        ///
        /// **The one list**, so a check renamed here is renamed in the command line and in the
        /// report at once, and a check added here reaches the runner and says what it may be asked
        /// of without anybody remembering to list it a second time. What a check answers is
        /// `StopWriter::checkHolds`, which needs the game and so stays in the harness.
        constexpr std::array sChecks{
            CheckRow{ Check::WalkTwice, "walk-twice", always },
            CheckRow{ Check::SurfacesDescribed, "surfaces-described", always },
            CheckRow{ Check::LightsPlaced, "lights-placed", always },
            CheckRow{ Check::GroundReaches, "ground-reaches", always },
            CheckRow{ Check::GroundStands, "ground-stands", always },
            CheckRow{ Check::LightsNotDoubled, "lights-not-doubled", always },
            CheckRow{ Check::StaticsNotDoubled, "statics-not-doubled", always },
            CheckRow{ Check::GatesAgree, "gates-agree", always },
            CheckRow{ Check::TexturesReadable, "textures-readable", always },
            CheckRow{ Check::RoomsHeld, "rooms-held", always },
            CheckRow{ Check::CameraStands, "camera-stands", standingStill },
            CheckRow{ Check::SkyNamed, "sky-named", namesItsSky },
            CheckRow{ Check::CrossingsAppend, "crossings-append", routed },
            CheckRow{ Check::FramesOverlap, "frames-overlap", unrouted },
            CheckRow{ Check::QueueHeld, "queue-held", held },
            CheckRow{ Check::Finite, "finite", always },
        };

        constexpr std::array<Check, sChecks.size()> sEvery = [] {
            std::array<Check, sChecks.size()> every{};
            for (std::size_t at = 0; at < sChecks.size(); ++at)
                every[at] = sChecks[at].mCheck;
            return every;
        }();

        // With `StopWriter::checkHolds`'s switch, which names every enumerator and no default, a
        // check added to the enum stops the build until it has a row here and an answer there.
        static_assert(
            Rtx::coversFromNought(sEvery), "every Check has one row in sChecks, and the rows are the enumerators");

        const CheckRow& rowOf(const Check check)
        {
            const auto found = std::find_if(
                sChecks.begin(), sChecks.end(), [check](const CheckRow& row) { return row.mCheck == check; });
            assert(found != sChecks.end() && "a check with no row: sEvery's assertion holds this shut");
            return *found;
        }
    }

    std::string_view checkName(const Check check)
    {
        return rowOf(check).mName;
    }

    std::span<const Check> everyCheck()
    {
        return sEvery;
    }

    bool canAsk(const Check check, const Stop& stop, const Rtx::RenderProfile& profile)
    {
        return rowOf(check).mCanAsk(stop, profile);
    }

    CellAddress::CellAddress(const std::string_view spelling)
        : mSpelling(spelling)
    {
        const std::size_t comma = spelling.find(',');
        if (comma == std::string_view::npos)
            return;

        const std::optional<int> x = wholeNumber<int>(trimmed(spelling.substr(0, comma)));
        const std::optional<int> y = wholeNumber<int>(trimmed(spelling.substr(comma + 1)));
        if (x.has_value() && y.has_value())
            *this = exterior(*x, *y);
    }

    CellAddress CellAddress::exterior(const int gridX, const int gridY)
    {
        CellAddress cell;
        cell.mSpelling = std::format("{},{}", gridX, gridY);
        cell.mExterior = true;
        return cell;
    }

    CellAddress CellAddress::interior(const std::string_view name)
    {
        CellAddress cell;
        cell.mSpelling = name;
        return cell;
    }

    std::uint32_t HeldFrames::under(const Rtx::FrameExtents& traced) const
    {
        const std::uint64_t tracedPixels = std::uint64_t{ traced.mRenderWidth } * traced.mRenderHeight;
        const std::uint64_t shownPixels = std::uint64_t{ traced.mOutputWidth } * traced.mOutputHeight;
        assert(shownPixels > 0 && "extents no renderer made");
        const std::uint64_t held = mAfter * tracedPixels / shownPixels;
        return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(held, 1, mMost));
    }

    void measureFrames(Stop& stop, const std::uint32_t frames)
    {
        stop.mSchedule.mSpec = BenchSpec{ .mRun = { .mFrames = frames }, .mWarm = { .mFrames = sHistoryFrames } };
    }

    osg::Vec3f Stand::getLook() const
    {
        assert(mEye.has_value());

        if (!mLook.has_value() || (*mLook - *mEye).length2() <= 0.0f)
            return *mEye + osg::Vec3f(0.0f, 1.0f, 0.0f);

        return *mLook;
    }

    bool Stand::lookAt(const osg::Vec3f& look)
    {
        if (!mEye.has_value())
            return false;

        mLook = look;
        return true;
    }

    osg::Vec3f Stand::getRotation() const
    {
        osg::Vec3f forward = getLook() - *mEye;
        forward.normalize();

        // Clamped because a normalised vector's z can land a bit past one, and `asin` answers a NaN
        // rather than a right angle when it does.
        return osg::Vec3f(-std::asin(std::clamp(forward.z(), -1.0f, 1.0f)), 0.0f, std::atan2(forward.x(), forward.y()));
    }

    osg::Vec3f Stand::forwardOf(const osg::Vec3f& rotation)
    {
        const float level = std::cos(rotation.x());
        return osg::Vec3f(std::sin(rotation.z()) * level, std::cos(rotation.z()) * level, -std::sin(rotation.x()));
    }

    osg::Vec3f Stand::getLevelAhead() const
    {
        const float yaw = getRotation().z();
        return osg::Vec3f(std::sin(yaw), std::cos(yaw), 0.0f);
    }

    Approach Stand::approachFrom(
        const float left, const float behind, const float step, const std::uint32_t frames) const
    {
        assert(mEye.has_value() && frames >= 2 && step > 0.0f);

        const osg::Vec3f ahead = getLevelAhead();
        const osg::Vec3f right(ahead.y(), -ahead.x(), 0.0f);
        const osg::Vec3f look = getLook();

        // **Arriving at the last frame and not before it**: the route moves the eye at each measured
        // frame, `frames - 1` steps from the first to the last.
        return Approach{
            .mFrom = Stand{ .mCell = mCell,
                .mEye = *mEye - right * left - ahead * behind,
                .mLook = look,
                .mLamps = mLamps,
                .mArms = mArms },
            .mRoute = Route{ .mTo = *mEye,
                .mLookTo = look,
                .mSpeed = std::hypot(left, behind) / (step * static_cast<float>(frames - 1)),
                .mWorldHeld = true },
        };
    }

    bool Actions::walksTwice() const
    {
        return mWalkTwice || std::ranges::find(mChecks, Check::WalkTwice) != mChecks.end();
    }

    std::optional<std::string> SessionRequest::stopAt(const Rtx::FrameResult& finished) const
    {
        if (!mPlayed || finished.mNotFinite.total() == 0)
            return std::nullopt;

        return std::format("frame {}: {}", finished.mFrame, describeNotFinite(finished.mNotFinite));
    }

    std::optional<std::string> SessionRequest::refusalUnder(const Rtx::UpscalerInput input) const
    {
        const Rtx::RenderProfile& profile = mSetup.mRun.mProfile;
        for (const Stop& stop : mStops)
        {
            const Rtx::Upscale upscale = stop.mSchedule.mUpscale.value_or(profile.mUpscale);
            const Rtx::ReconstructionRequest asked = stop.mSchedule.mReconstruction.value_or(profile.mReconstruction);
            if (!asked.mDenoise && !Rtx::rendererFilters(upscale, input))
                return std::format(
                    "{} asks for the light unfiltered at the {} mode, and the upscaler filters the "
                    "light itself there: name --filter=true, --upscale=off or another --upscaler",
                    stop.mName, Rtx::sUpscaleNames.name(upscale));
        }
        return std::nullopt;
    }

    void SessionRequest::settleUnder(const Rtx::FrameExtents& traced)
    {
        for (Stop& stop : mStops)
        {
            Schedule& schedule = stop.mSchedule;
            if (!schedule.mHeld.has_value())
                continue;

            const std::uint32_t frames = schedule.mHeld->under(traced);
            schedule.mSpec.mRun = BenchSpan{ .mFrames = frames };
            schedule.mAccumulate = frames;
            schedule.mHeld.reset();
        }

        if (mTraced != nullptr)
            *mTraced = traced;
    }

    MWRender::RunSetup SessionRequest::rendererSetup() const
    {
        MWRender::RunSetup setup = mSetup;
        std::vector<Rtx::Upscale>& reserved = setup.mRun.mReservedUpscales;
        reserved.assign(1, setup.mRun.mProfile.mUpscale);
        for (const Stop& stop : mStops)
            if (stop.mSchedule.mUpscale.has_value())
                reserved.push_back(*stop.mSchedule.mUpscale);

        std::ranges::sort(reserved);
        reserved.erase(std::ranges::unique(reserved).begin(), reserved.end());
        return setup;
    }
}
