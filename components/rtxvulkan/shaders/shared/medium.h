#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_MEDIUM_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_MEDIUM_H

#include <components/rtx/shaders/brdf.h>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/portable.h>

// What a stretch of a participating medium does to a source spread along it: the one closed form
// the fog's froxels and the water's column both integrate a stretch by, and the stretches each
// integrates. Included verbatim by both sides, for the reason `fogvolume.h` is, and so a test holds
// each closed form against the integral it stands for.

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

    /// What one slice of a column scatters and takes out, once everything that lights it is applied, each
    /// times the density (`fogDensityAt`): the air's own colour with the moons and the lamps in it, the
    /// sun's transport with the irradiance and the phase left off, and the density itself, which the
    /// weather's extinction makes per world unit.
    ///
    /// **The products and not their factors**: what a stretch integrates is the extinction times
    /// the light, and the two move against each other — a froxel the bank's edge crosses is dense and
    /// shadowed on one side and thin and lit on the other — so the mean of their product is not the
    /// product of their means, which is what a history and a tent of the two apart came to.
    ///
    /// **A sample at the slice's middle, and not a constant over the slice.** The volume holds one of
    /// these per froxel, and what a froxel's value is is a property of one point in it, averaged over
    /// draws — so between two of them the air is what a sampler says it is between two texels: the
    /// line from one to the next. `fogThrough` integrates that line, and `fogintegrate.comp` and
    /// `fogVolumeAlong` both step through it, so a column's accumulation and a pixel's read of the
    /// slice it ends in agree exactly at the slice's far edge.
    ///
    /// **Constant over a slice instead, the grid drew itself.** The accumulation was then a straight
    /// line across every slice with a corner at every edge — or, reconstructed with a cubic to hide the
    /// corners, a bump in the middle of every slice — and either is a pattern with the slices' own
    /// period, laid on the ground as shells around the eye wherever two neighbouring slices held
    /// different air. Banked air holds different air in neighbouring slices everywhere.
    struct FogSlice
    {
        vec3 mSource;
        float mDensity;
        float mSunSource;
    };

    /// The accumulation up to a slice's far edge, as `fogintegrate.comp` packs it and
    /// `fogVolumeAlong` reads it: what scattered in, what is left of the ray, and the sun's transport
    /// alone.
    struct FogColumn
    {
        vec3 mScattered;
        float mTransmittance;
        float mSunward;
    };

    RTX_SHADER FogSlice fogSliceBetween(FogSlice from, FogSlice to, float fraction)
    {
        FogSlice slice;
        slice.mSource = mix(from.mSource, to.mSource, fraction);
        slice.mDensity = mix(from.mDensity, to.mDensity, fraction);
        slice.mSunSource = mix(from.mSunSource, to.mSunSource, fraction);
        return slice;
    }

    /// `column` carried `length` units further through air of `slice`, under the weather's `extinction` per unit of
    /// density, accumulating what it scattered in and taking off what it lost.
    ///
    /// **The source is the extinction times the light, so what a stretch scatters in is
    /// `T (1 - e^-σd) / σ` of it** — the transmittance's share the source keeps over the stretch —
    /// which `mediumKept` holds to its digits however thin the air, and which is the stretch's length times
    /// the transmittance where there is no air at all. What the transmittance loses is the same stretch
    /// times `σ`.
    ///
    /// **The transmittance exact over a stretch the line does not bend in, and the light scattered in
    /// the midpoint rule's, second order in the stretch.** A linear density's mean is its middle, so the
    /// optical depth is exact; the light a linear source scatters through a linear density has a closed
    /// form only through `erf`, and the middle's source over the stretch's loss is its midpoint estimate.
    /// Which is why both callers cut a slice at its middle: the line from one slice's sample to the next
    /// bends only at the samples, so each half of a slice is one straight piece.
    RTX_SHADER FogColumn fogThrough(FogColumn column, FogSlice slice, float length, float extinction)
    {
        const float depth = slice.mDensity * extinction * length;
        const float share = column.mTransmittance * mediumKept(depth);
        const float carried = share * length * extinction;

        column.mScattered += slice.mSource * carried;
        column.mSunward += carried * slice.mSunSource;
        column.mTransmittance -= share * depth;
        return column;
    }

    /// What a stretch of water `path` long along `direction` gathers of a light arriving `slant` units
    /// of water per unit of depth, as a share of the stretch: `exp(-o k h)`'s factor along the ray,
    /// `(1 - exp(-o g L)) / g` with `g = 1 - k d.z`, in `mediumKept`'s form, which holds its digits
    /// however short the stretch or level the ray, and which a negative `g` — looking up toward the
    /// light — does not trouble.
    RTX_SHADER vec3 gatheredAlong(vec3 direction, float slant, float path)
    {
        const float rising = (1.0f - slant * direction[2]) * path;
        return vec3(WATER_EXTINCTION[0] * path * mediumKept(WATER_EXTINCTION[0] * rising),
            WATER_EXTINCTION[1] * path * mediumKept(WATER_EXTINCTION[1] * rising),
            WATER_EXTINCTION[2] * path * mediumKept(WATER_EXTINCTION[2] * rising));
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
