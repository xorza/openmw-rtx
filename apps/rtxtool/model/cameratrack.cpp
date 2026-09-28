#include "cameratrack.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include <components/rtx/contract.hpp>

namespace RtxTool
{
    namespace
    {
        constexpr std::size_t sYaw = 0;
        constexpr std::size_t sPitch = 1;
        constexpr std::size_t sHours = 2;

        constexpr double sTurn = 2.0 * std::numbers::pi;

        /// What a crossing of the sky eases by, so it starts and lands as gently as the camera does.
        float smoothstep(const float u)
        {
            return u * u * (3.0f - 2.0f * u);
        }
    }

    float shortestTurn(const float from, const float to)
    {
        return static_cast<float>(std::remainder(static_cast<double>(to) - static_cast<double>(from), sTurn));
    }

    float hoursForward(const float from, const float to)
    {
        return static_cast<float>(std::fmod(static_cast<double>(to) - static_cast<double>(from) + 24.0, 24.0));
    }

    void SkyRun::pose(const std::uint32_t frame, TrackPose& into) const
    {
        if (mHoursPerFrame.has_value())
            into.mHoursOn = static_cast<double>(frame + (mFirstFrame > 0 ? 1u : 0u)) * *mHoursPerFrame;

        if (!mWeathers.empty())
            turnAt(mFirstFrame + frame, into);
    }

    void SkyRun::turnAt(const std::uint32_t frame, TrackPose& into) const
    {
        const std::uint32_t period = mHoldFrames + mCrossingFrames;
        const std::uint32_t turn = frame / period;
        const std::uint32_t within = frame % period;
        const auto count = static_cast<std::uint32_t>(mWeathers.size());

        into.mWeather = mWeathers[turn % count];
        into.mNextWeather = within < mHoldFrames ? into.mWeather : mWeathers[(turn + 1) % count];
        into.mCrossed = into.mNextWeather == into.mWeather
            ? 0.0f
            : smoothstep(static_cast<float>(within - mHoldFrames) / static_cast<float>(mCrossingFrames));
    }

    CameraTrack::CameraTrack(const std::span<const TrackKey> keys, CameraPath path, const Cruise cruise, SkyRun sky)
        : mPath(std::move(path))
        , mCruise(cruise)
        , mSky(std::move(sky))
    {
        Rtx::contract(!keys.empty(), "a camera track needs a key");
        Rtx::contract(mPath.getSegments() + 1 == keys.size(), "a camera track on another track's path");
        Rtx::contract(mSky.mCrossingFrames > 0, "a sky that crosses in no frames");
        Rtx::contract(keys.back().mFrame == std::floor(keys.back().mFrame), "a take that ends between two frames");

        mKnots.reserve(keys.size());
        for (std::size_t at = 0; at < keys.size(); ++at)
        {
            const TrackKey& key = keys[at];
            Knot knot{ .mFrame = key.mFrame, .mWeather = key.mWeather };
            knot.mValue = { key.mRotation.z(), key.mRotation.x(), 0.0 };

            if (at > 0)
            {
                const TrackKey& before = keys[at - 1];
                const Knot& last = mKnots.back();
                Rtx::contract(key.mFrame > before.mFrame, "a camera track's keys go back in time");

                knot.mValue[sYaw]
                    = last.mValue[sYaw] + static_cast<double>(shortestTurn(before.mRotation.z(), key.mRotation.z()));
                knot.mValue[sHours] = last.mValue[sHours] + static_cast<double>(hoursForward(before.mHour, key.mHour));
            }

            mKnots.push_back(knot);
        }

        const std::size_t last = mKnots.size() - 1;
        const auto secant = [&](const std::size_t from, const std::size_t channel) {
            const Knot& a = mKnots[from];
            const Knot& b = mKnots[from + 1];
            return (b.mValue[channel] - a.mValue[channel]) / (b.mFrame - a.mFrame);
        };

        for (std::size_t channel = 0; channel < sChannels; ++channel)
        {
            // Catmull-Rom's tangent inside, and none where the secants either side disagree in sign
            // or one of them is flat: the knot is an extreme of the channel, and a curve through it
            // with any slope would pass it.
            for (std::size_t at = 1; at < last; ++at)
            {
                if (keys[at].mRests || secant(at - 1, channel) * secant(at, channel) <= 0.0)
                    continue;

                const Knot& before = mKnots[at - 1];
                const Knot& after = mKnots[at + 1];
                mKnots[at].mSlope[channel]
                    = (after.mValue[channel] - before.mValue[channel]) / (after.mFrame - before.mFrame);
            }

            // Scaled into the circle of radius three, which keeps each segment monotone.
            for (std::size_t at = 0; at < last; ++at)
            {
                const double slope = secant(at, channel);
                if (slope == 0.0)
                    continue;

                double& from = mKnots[at].mSlope[channel];
                double& to = mKnots[at + 1].mSlope[channel];
                const double alpha = from / slope;
                const double beta = to / slope;
                const double radius2 = alpha * alpha + beta * beta;
                if (radius2 <= 9.0)
                    continue;

                const double tau = 3.0 / std::sqrt(radius2);
                from = tau * alpha * slope;
                to = tau * beta * slope;
            }
        }
    }

    std::uint32_t CameraTrack::getFrames() const
    {
        return static_cast<std::uint32_t>(mKnots.back().mFrame) + 1;
    }

    TrackPose CameraTrack::pose(const std::uint32_t frame) const
    {
        const double at = static_cast<double>(frame);
        const auto after = std::upper_bound(
            mKnots.begin(), mKnots.end(), at, [](double f, const Knot& knot) { return f < knot.mFrame; });

        std::array<double, sChannels> value{};
        osg::Vec3d eye;
        TrackPose pose;
        if (after == mKnots.begin() || after == mKnots.end())
        {
            const bool first = after == mKnots.begin();
            const Knot& knot = first ? mKnots.front() : mKnots.back();
            value = knot.mValue;
            eye = mPath.getEye(first ? 0 : mKnots.size() - 1);
            pose.mWeather = knot.mWeather;
            pose.mNextWeather = knot.mWeather;
        }
        else
        {
            const Knot& a = *(after - 1);
            const Knot& b = *after;
            const auto segment = static_cast<std::size_t>(after - mKnots.begin() - 1);
            const double span = b.mFrame - a.mFrame;
            const double into = at - a.mFrame;
            const double u = into / span;

            const CruiseLeg leg{ .mLength = mPath.getLength(segment),
                .mFromRest = mPath.restsAt(segment),
                .mToRest = mPath.restsAt(segment + 1) };
            eye = mPath.at(segment, leg.mLength > 0.0 ? mCruise.coveredAt(leg, span, into) : 0.0);
            const double u2 = u * u;
            const double u3 = u2 * u;

            // The Hermite basis with `h00` folded into `h01` as `1 − h01`, so a key is its own value
            // to the bit and a flat segment stays flat: `h00·a + h01·a` rounds off `a`, and a clock
            // standing still read 12.000000000000002 and then 12.
            const double h10 = u3 - 2.0 * u2 + u;
            const double h01 = -2.0 * u3 + 3.0 * u2;
            const double h11 = u3 - u2;

            for (std::size_t channel = 0; channel < sChannels; ++channel)
                value[channel] = a.mValue[channel] + h01 * (b.mValue[channel] - a.mValue[channel])
                    + span * (h10 * a.mSlope[channel] + h11 * b.mSlope[channel]);

            pose.mWeather = a.mWeather;
            pose.mNextWeather = b.mWeather;
            pose.mCrossed = a.mWeather == b.mWeather ? 0.0f : smoothstep(static_cast<float>(u));
        }

        pose.mEye = osg::Vec3f(eye);
        pose.mRotation = osg::Vec3f(
            static_cast<float>(value[sPitch]), 0.0f, static_cast<float>(std::remainder(value[sYaw], sTurn)));
        pose.mHoursOn = value[sHours];
        mSky.pose(frame, pose);
        return pose;
    }
}
