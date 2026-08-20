#include "fogbuilder.hpp"

#include <cmath>

#include <components/esm3/loadcell.hpp>
#include <components/settings/values.hpp>

#include "lightbuilder.hpp"

namespace RtxBridge
{
    float fogExtinction(float depth)
    {
        // The original engine reads a depth of zero as no fog at all rather than as a ramp starting
        // at the view distance, and so does this.
        if (!(depth > 0.0f))
            return 0.0f;

        return std::log(2.0f) / (Settings::camera().mViewingDistance * (1.0f - 0.5f * depth));
    }

    Fog interiorFog(const ESM::Cell& cell)
    {
        if (!cell.mHasAmbi)
            return {};

        return Fog{
            .mColour = decodeColour(cell.mAmbi.mFog),
            .mExtinction = fogExtinction(cell.mAmbi.mFogDensity),
        };
    }
}
