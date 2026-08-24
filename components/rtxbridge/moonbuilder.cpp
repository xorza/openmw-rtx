#include "moonbuilder.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <osg/Quat>

#include <components/fallback/fallback.hpp>
#include <components/rtx/shaders/scene.h>

namespace RtxBridge
{
    namespace
    {
        std::string_view nameOf(Moon moon)
        {
            return moon == Moon::Masser ? "Masser" : "Secunda";
        }

        float setting(Moon moon, std::string_view field)
        {
            return Fallback::Map::getFloat("Moons_" + std::string(nameOf(moon)) + "_" + std::string(field));
        }

        /// Everything `MWWorld::MoonModel` reads, read once.
        ///
        /// The two adjustments in the constructor are the engine's and carry its reasons with them:
        /// a moon slower than a hemisphere in twenty-three hours could not finish its arc in a day,
        /// and a daily increment past twenty-four would advance it more than a whole rotation.
        struct Model
        {
            float mFadeInStart;
            float mFadeInFinish;
            float mFadeOutStart;
            float mFadeOutFinish;
            float mAxisOffset;
            float mSpeed;
            float mDailyIncrement;
            float mFadeStartAngle;
            float mFadeEndAngle;
            float mEarlyFadeAngle;

            explicit Model(Moon moon)
                : mFadeInStart(setting(moon, "Fade_In_Start"))
                , mFadeInFinish(setting(moon, "Fade_In_Finish"))
                , mFadeOutStart(setting(moon, "Fade_Out_Start"))
                , mFadeOutFinish(setting(moon, "Fade_Out_Finish"))
                , mAxisOffset(setting(moon, "Axis_Offset"))
                , mSpeed(std::max(setting(moon, "Speed"), 180.0f / 23.0f / 15.0f))
                , mDailyIncrement(std::fmod(setting(moon, "Daily_Increment"), 24.0f))
                , mFadeStartAngle(setting(moon, "Fade_Start_Angle"))
                , mFadeEndAngle(setting(moon, "Fade_End_Angle"))
                , mEarlyFadeAngle(setting(moon, "Moon_Shadow_Early_Fade_Angle"))
            {
            }

            /// Degrees travelled in `hours`. Fifteen an hour is a rotation a day, so the speed is a
            /// count of whole rotations a day rather than a rate.
            float rotation(float hours) const { return 15.0f * mSpeed * hours; }

            /// The hour the moon rises on `day`.
            ///
            /// The start day of sixteen is 16 Last Seed, which is where Morrowind's own count is
            /// anchored: seventeen increments have already happened by the time a new game begins.
            float riseHour(int day) const
            {
                if (mDailyIncrement == 0.0f)
                    return 0.0f;

                constexpr int startDay = 16;

                // What makes the rise hour a twenty-four day loop: the increments the engine skips,
                // multiplied by however many loops have gone by.
                const float missed = (24.0f - std::abs(24.0f / mDailyIncrement))
                    * std::floor(static_cast<float>(day + startDay) / 24.0f);

                // No modulo after the last increment: a rise hour at or past twenty-four is how the
                // engine says "not until tomorrow", and the angle below asks that question.
                return mDailyIncrement
                    + std::fmod((static_cast<float>(day - 1 + startDay) - missed) * mDailyIncrement, 24.0f);
            }

            /// How far along its arc the moon is, in degrees: zero at one horizon and 180 at the
            /// other, and zero again once it has set.
            float angle(int day, float hour) const
            {
                const float riseToday = riseHour(day);
                float travelled = 0.0f;

                if (hour < riseToday)
                {
                    // It may still be up from yesterday, whose rise hour is one increment earlier.
                    const float riseYesterday = riseToday - mDailyIncrement;
                    if (riseYesterday < 24.0f)
                    {
                        // The engine offsets the increment by one where yesterday's visible point
                        // crossed into today, and holds that offset until the loop restarts.
                        const float earlyFade = mFadeEndAngle - mEarlyFadeAngle;
                        const float toVisible = earlyFade / rotation(1.0f);
                        const float offset = riseYesterday + toVisible > 24.0f ? mDailyIncrement : 0.0f;

                        const float yesterday = rotation(24.0f - (riseYesterday + offset));
                        if (yesterday < 180.0f)
                            travelled = rotation(hour) + yesterday;
                    }
                }
                else
                {
                    travelled = rotation(hour - riseToday);
                }

                return travelled >= 180.0f ? 0.0f : travelled;
            }

            /// How transparent the hour alone makes the moon. The engine culls it a minute before
            /// the fade finishes, which is why the finish is pulled back by one.
            float hourlyAlpha(float hour) const
            {
                constexpr float oneMinute = 0.0167f;
                const float fadeOutFinish = mFadeOutFinish - oneMinute;

                if (hour >= mFadeOutStart && hour < fadeOutFinish)
                    return (fadeOutFinish - hour) / (fadeOutFinish - mFadeOutStart);
                if (hour >= fadeOutFinish && hour < mFadeInStart)
                    return 0.0f;
                if (hour >= mFadeInStart && hour < mFadeInFinish)
                    return (hour - mFadeInStart) / (mFadeInFinish - mFadeInStart);
                return 1.0f;
            }

            /// And how transparent its place on the arc makes it — an arc either side of the fade
            /// end angles, over which it arrives and leaves.
            float earlyShadowAlpha(float angle) const
            {
                const float rising = mFadeEndAngle - mEarlyFadeAngle;
                const float endSetting = 180.0f - mFadeEndAngle;
                const float setting = endSetting + mEarlyFadeAngle;

                if (angle >= rising && angle < mFadeEndAngle)
                    return (angle - rising) / mEarlyFadeAngle;
                if (angle >= mFadeEndAngle && angle < endSetting)
                    return 1.0f;
                if (angle >= endSetting && angle < setting)
                    return (setting - angle) / mEarlyFadeAngle;
                return 0.0f;
            }

            bool isVisible(int day, float hour) const
            {
                return hourlyAlpha(hour) > 0.0f && earlyShadowAlpha(angle(day, hour)) > 0.0f;
            }

            /// The hour the phase turns over on `day`.
            ///
            /// Morrowind holds a phase change back until the moon is invisible — at midnight, or one
            /// increment past the angle where it has already faded.
            float phaseHour(int day) const
            {
                if (!isVisible(day, 0.0f))
                    return 0.0f;

                const float faded = (180.0f - mFadeEndAngle) + mEarlyFadeAngle;
                return ((faded - angle(day, 0.0f)) / rotation(1.0f)) + std::max(mDailyIncrement, 0.0f);
            }

            /// Which of the eight phases, counted from full.
            ///
            /// The game starts full on 16 Last Seed and wanes from the seventeenth on a three-day
            /// cycle; a moon that has not risen yet today is still showing yesterday's.
            int phase(int day, float hour) const
            {
                const int counted = hour < phaseHour(day) ? day : day + 1;
                return (counted / 3) % 8;
            }
        };

        /// The mean opaque texel of `tx_masser_full.dds` and `tx_secunda_full.dds`, linear.
        ///
        /// Measured off the shipped portraits rather than chosen: one red, one grey, and the ratio
        /// between them is what tells the two moons apart at a glance.
        const osg::Vec3f sMasserFace(0.0332f, 0.0099f, 0.0123f);
        const osg::Vec3f sSecundaFace(0.0440f, 0.0373f, 0.0295f);

        /// What the brightest channel of a full Masser comes back with.
        ///
        /// **Pinned, and not by taste.** A real full moon is a 640,000th of the sun and there is no
        /// scale this renderer could put both on, so the number has to be chosen — and what chooses
        /// it is that a moon bright enough to blow all three channels is a white disc whatever
        /// colour it was given. This lands Masser's red at the top of the range with its blue a
        /// fifth of that, which is the most red a moon can be and still be a moon.
        constexpr float sPeakRadiance = 0.18f;

        /// Half the angle a moon of this size subtends, out of the geometry the game's own renderer
        /// builds: `Moons_<name>_Size / 125 * 450` scales a quad of half-extent 0.5, a thousand
        /// units away.
        float angularRadiusOf(Moon moon)
        {
            const float halfWidth = 0.5f * 450.0f * setting(moon, "Size") / 125.0f;
            return std::atan(halfWidth / 1000.0f);
        }
    }

    Rtx::Shaders::MoonDisc describeMoon(const MoonPlacement& placement)
    {
        return Rtx::Shaders::MoonDisc{
            .mDirection = placement.mDirection,
            .mRight = placement.mRight,
            .mUp = placement.mUp,
            .mColour = placement.mColour,
            .mAngularRadius = placement.mAngularRadius,
            .mPhaseAngle = placement.mPhaseAngle,
            .mAlpha = placement.mAlpha,
        };
    }

    float moonAngularRadius(Moon moon)
    {
        return angularRadiusOf(moon);
    }

    MoonPlacement makeMoon(Moon moon, int day, float hour)
    {
        const Model model(moon);
        const float degrees = model.angle(day, hour);

        // `Moon::setState`'s own two rotations (`apps/openmw/mwrender/gl/skyutil.cpp:900`): the arc
        // tips the moon up from the horizon about +X, and the axis offset swings that whole arc
        // about the zenith so the two moons rise in different places and their paths cross.
        const float alongArc = osg::DegreesToRadians(degrees);
        const float aboutZenith = osg::DegreesToRadians(model.mAxisOffset);

        const osg::Quat arc(alongArc, osg::Vec3f(1.0f, 0.0f, 0.0f));
        const osg::Quat swing(aboutZenith, osg::Vec3f(0.0f, 0.0f, 1.0f));

        // **The face's own attitude, and not a billboard's.** The quad the game draws starts facing
        // down, so its rotation carries the same quarter turn — which is what leaves the portrait
        // upright against the moon's arc rather than against the horizon.
        const osg::Quat attitude = osg::Quat(alongArc - 0.5f * osg::PIf, osg::Vec3f(1.0f, 0.0f, 0.0f)) * swing;

        MoonPlacement placement{
            .mDirection = arc * swing * osg::Vec3f(0.0f, 1.0f, 0.0f),
            .mRight = attitude * osg::Vec3f(1.0f, 0.0f, 0.0f),
            .mUp = attitude * osg::Vec3f(0.0f, 1.0f, 0.0f),
            .mAngularRadius = angularRadiusOf(moon),

            // Eight painted phases are eight steps of a half turn each way, counted from full — so
            // the index is the angle, and the sign of its sine is the limb the light is on.
            .mPhaseAngle = static_cast<float>(model.phase(day, hour)) * 0.25f * osg::PIf,

            .mAlpha = model.earlyShadowAlpha(degrees) * model.hourlyAlpha(hour),

            // One scale for both faces, taken off Masser's brightest channel — see `sPeakRadiance`.
            .mColour = (moon == Moon::Masser ? sMasserFace : sSecundaFace) * (sPeakRadiance / sMasserFace.x()),
        };

        placement.mDirection.normalize();
        placement.mRight.normalize();
        placement.mUp.normalize();
        return placement;
    }
}
