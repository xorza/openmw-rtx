#include "cruise.hpp"

#include <algorithm>
#include <limits>
#include <vector>

#include <components/crashcatcher/crash.hpp>

namespace RtxTool
{
    namespace
    {
        /// How far an ease has carried the eye `u` of the way through it, in units of the cruising
        /// speed times the ease: the integral of smoothstep, a half at its end.
        double eased(const double u)
        {
            return u * u * u * (1.0 - 0.5 * u);
        }

        /// The speed above which `leg`'s eases no longer fit inside it: where `n·E/2` reaches `L/v`.
        double easesFitUpTo(const CruiseLeg& leg, const double ease)
        {
            return 2.0 * leg.mLength / (static_cast<double>(leg.getRests()) * ease);
        }
    }

    double Cruise::timeFor(const CruiseLeg& leg, const double speed) const
    {
        Crash::contract(leg.mLength > 0.0 && speed > 0.0, "a leg timed with no length or no speed");
        const double cruising = leg.mLength / speed;
        return cruising + std::min(static_cast<double>(leg.getRests()) * mEase / 2.0, cruising);
    }

    double Cruise::speedFor(const CruiseLeg& leg, const double time) const
    {
        Crash::contract(leg.mLength > 0.0 && time > 0.0, "a leg flown in no time or no length");
        const double rests = static_cast<double>(leg.getRests());
        return time >= rests * mEase ? leg.mLength / (time - rests * mEase / 2.0) : 2.0 * leg.mLength / time;
    }

    double Cruise::speedFor(const std::span<const CruiseLeg> legs, const double time) const
    {
        Crash::contract(!legs.empty() && time > 0.0, "a flight of no legs, or in no time");

        std::vector<double> fits;
        fits.reserve(legs.size());
        for (const CruiseLeg& leg : legs)
            if (leg.getRests() > 0 && mEase > 0.0)
                fits.push_back(easesFitUpTo(leg, mEase));
        std::sort(fits.begin(), fits.end());

        // From the fastest interval down, `above` being how many of the speeds lie under it.
        for (std::size_t above = fits.size();; --above)
        {
            const double low = above == 0 ? 0.0 : fits[above - 1];
            const double high = above == fits.size() ? std::numeric_limits<double>::infinity() : fits[above];

            double perSpeed = 0.0;
            double fixed = 0.0;
            for (const CruiseLeg& leg : legs)
            {
                if (leg.getRests() == 0 || mEase <= 0.0)
                    perSpeed += leg.mLength;
                else if (easesFitUpTo(leg, mEase) >= high)
                {
                    perSpeed += leg.mLength;
                    fixed += static_cast<double>(leg.getRests()) * mEase / 2.0;
                }
                else
                    perSpeed += 2.0 * leg.mLength;
            }

            if (time > fixed)
            {
                const double speed = perSpeed / (time - fixed);
                if (speed > low && speed <= high)
                    return speed;
            }

            if (above == 0)
                break;
        }

        Crash::fatal("no speed fills a flight's time, which a total falling from infinity to nought always has");
    }

    double Cruise::coveredAt(const CruiseLeg& leg, const double time, const double at) const
    {
        const double speed = speedFor(leg, time);
        const double rests = static_cast<double>(leg.getRests());
        const double ease = rests == 0.0 || mEase <= 0.0 ? 0.0 : time >= rests * mEase ? mEase : time / rests;
        const double within = std::clamp(at, 0.0, time);

        if (leg.mFromRest && ease > 0.0 && within < ease)
            return speed * ease * eased(within / ease);
        if (leg.mToRest && ease > 0.0 && within > time - ease)
            return leg.mLength - speed * ease * eased((time - within) / ease);
        return speed * (leg.mFromRest ? within - ease / 2.0 : within);
    }
}
