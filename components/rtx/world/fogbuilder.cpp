#include "fogbuilder.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <osg/Vec3f>

#include <components/rtx/shaders/colour.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>

namespace Rtx
{
    namespace
    {
        /// The optical depth `sigma * over` at which a medium's mean transmittance over `over` is
        /// `mean`, below one: the root of `(1 - e^-x) / x = mean`. **Newton's method from `1 / mean`**,
        /// right of the root, where `1 - e^-x - mean * x` is concave and falling, so each step lands nearer
        /// and never past it; it stops when a step no longer moves the estimate down.
        double opticalDepthForMean(const double mean)
        {
            double x = 1.0 / mean;
            while (true)
            {
                const double step = (1.0 - std::exp(-x) - mean * x) / (std::exp(-x) - mean);
                const double next = x - step;
                if (!(next < x))
                    return x;
                x = next;
            }
        }

        /// The optical depth over the view that halves where the original's ramp does.
        double opticalDepthAtHalfPoint(const double depth)
        {
            return std::numbers::ln2 / (1.0 - 0.5 * depth);
        }

        /// The optical depth over the view that gives the ramp's own mean visibility, for a depth
        /// beyond one, where the ramp starts behind the eye: `1 / (2 * depth)`.
        double opticalDepthForRampMean(const double depth)
        {
            return opticalDepthForMean(0.5 / depth);
        }
    }

    float deepestHalfPointDepth()
    {
        // Bisected once, between a depth of one, where the half point is the thinner, and two,
        // where it is infinite, until the bracket is two neighbouring doubles.
        static const float deepest = [] {
            double low = 1.0;
            double high = 2.0;
            while (true)
            {
                const double middle = 0.5 * (low + high);
                if (!(middle > low && middle < high))
                    return static_cast<float>(low);
                if (opticalDepthAtHalfPoint(middle) < opticalDepthForRampMean(middle))
                    low = middle;
                else
                    high = middle;
            }
        }();
        return deepest;
    }

    float fogExtinction(float depth, float over)
    {
        // The original engine reads a depth of zero as no fog at all rather than as a ramp starting
        // at the view distance, and so does this.
        if (!(depth > 0.0f))
            return 0.0f;

        // **Only an infinite depth is the densest air**, a half-life of the shortest distance the
        // world measures: a cell's `AMBI` density reaches here unclamped, and the mean rule's
        // extinction grows without bound as the depth does.
        constexpr double densest = std::numbers::ln2;
        if (std::isinf(depth))
            return static_cast<float>(densest);

        const double opticalDepth
            = depth <= deepestHalfPointDepth() ? opticalDepthAtHalfPoint(depth) : opticalDepthForRampMean(depth);
        return static_cast<float>(std::min(opticalDepth / double{ over }, densest));
    }

    float fogLift(float depth, float wind)
    {
        // **The depth read by `fogExtinction`'s rule**, which takes nought, a negative or not a number for
        // no fog. Such a layer stands the clear one: the shaders divide by the lift, and nought over nought
        // is the air at the fog's base, the sea outdoors.
        if (!(depth > 0.0f))
            return 1.0f;

        const float lifting = wind > 0.0f ? wind : 0.0f;
        return depth / sClearFogDepth * (1.0f + lifting * sFogWindLift);
    }

    osg::Vec3f fogColour(const osg::Vec3f& skyMean, const osg::Vec3f& hue)
    {
        const float brightest = std::max({ hue.x(), hue.y(), hue.z(), 1e-4f });
        return hue * ((skyMean * Shaders::LUMINANCE_WEIGHTS) / brightest);
    }

    Fog exteriorFog(const osg::Vec3f& colour, float depth, float wind, float reach)
    {
        return Fog{
            .mColour = colour,
            .mExtinction = fogExtinction(depth, reach),
            .mUniform = Shaders::FOG_EXTERIOR_EVEN,

            // The same record read a second time: `fogExtinction` reads it as the view-range ramp
            // and takes a half-life, this reads it as a *depth* and takes a layer height.
            .mLift = fogLift(depth, wind),
            .mWind = wind,
            .mEdge = reach,
        };
    }

    Fog quasiExteriorFog(const osg::Vec3f& colour, float depth, float wind, float reach)
    {
        Fog air = exteriorFog(colour, depth, wind, reach);
        air.mEdge = 0.0f;

        return air;
    }

    Fog roomFog(const osg::Vec3f& colour, float depth)
    {
        return Fog{
            .mColour = colour,

            // A room is measured against one fixed distance, not how much world is built outside it or
            // how much the player asked to see (`sInteriorFogReach`).
            .mExtinction = fogExtinction(depth, sInteriorFogReach),

            // A room is smaller than one bank of fog, and its air is still.
            .mUniform = 1.0f,

            // A room has no weather to stand its air up or carry it; its layer is the one `FOG_HEIGHT` names.
            .mLift = 1.0f,
            .mWind = 0.0f,

            .mEdge = 0.0f,
        };
    }
}
