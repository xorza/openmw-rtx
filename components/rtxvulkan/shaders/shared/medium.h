#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_MEDIUM_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_MEDIUM_H

#include <components/rtx/shaders/brdf.h>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/portable.h>

// What a stretch of a participating medium does to a source spread along it: the one closed form
// the fog's froxels and the water's column both integrate a stretch by. Included verbatim by both
// sides, for the reason `fogvolume.h` is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// What a stretch of optical depth `x` keeps of a source spread evenly along it, per unit of its
    /// length: `(1 - e^-x) / x`, one at nought. **Negative `x` too**, a stretch along which the source
    /// grows faster than the medium takes it away — the water's beam looking up toward the sun.
    ///
    /// **A series inside a sixteenth either side of nought**, where the closed form loses its digits:
    /// `1 - e^-x` takes the rounding of `e^-x`, six parts in a hundred million, as a share of `x`,
    /// which at a hundred-thousandth is half of it. Four terms of the series leave `x^4 / 120`, a part
    /// in ten million at the switch, and the closed form a part in a million there.
    RTX_SHADER float mediumKept(float x)
    {
        // `e^-x` as `2^(-x log2 e)`, the one exponential both sides spell alike.
        return abs(x) < 0.0625f ? 1.0f - x * (0.5f - x * (1.0f / 6.0f - x / 24.0f))
                                : (1.0f - exp2(-x * 1.4426950408889634f)) / x;
    }

    /// What crossing a flat water surface leaves of a light arriving from `toward`, unit toward the
    /// light: `1 - F` at the angle it arrives at, which a surface under the water is lit through, and
    /// that times `cos(i) / cos(t)`, what the beam's irradiance across its own line becomes once the
    /// surface has bent it toward the vertical, which the water's column scatters.
    struct WaterCrossing
    {
        float mInto;
        float mBeam;
    };

    /// **By the Schlick Fresnel the water's own reflection takes** (`WATER_F0`), so what the surface
    /// reflects and what it lets in sum to the light. **Worked out where it is used, from the
    /// direction alone**, and not carried beside the direction in `SkySource`: a few operations a use,
    /// where a stored pair is a second statement of the direction a writer can leave stale. A light on
    /// or under the horizon arrives at grazing, where Schlick's weight is one and nothing gets in.
    RTX_SHADER WaterCrossing waterCrossingOf(vec3 toward)
    {
        const float arriving = max(toward[2], 0.0f);
        const float into = 1.0f - fresnelSchlick(WATER_F0, 1.0f, schlickWeight(arriving));
        const float sine = sqrt(max(1.0f - arriving * arriving, 0.0f)) / WATER_IOR;
        WaterCrossing crossing;
        crossing.mInto = into;
        crossing.mBeam = into * arriving / sqrt(1.0f - sine * sine);
        return crossing;
    }

#ifdef RTX_HOST
}
#endif

#endif
