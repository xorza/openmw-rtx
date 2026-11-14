#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FOG_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FOG_GLSL

// The air between the eye and everything else: what it scatters toward the eye, and what a ray loses crossing
// it. How much of it there is at a point is `fogfield.glsl`'s, which a pass that casts no ray reads alone.
//
// Two elements that answer different questions: the weather's air is Morrowind's own record of what the sky
// is like; the world's edge is this renderer's own, and covers the ring where its ground stops.

#extension GL_EXT_control_flow_attributes : require

#include "camera.h"
#include "colour.h"
#include "look.h"
#include "scene.h"
#include "sky.h"
#include "bindings.glsl"
#include "frame.glsl"
#include "fogfield.glsl"
#include "froxel.glsl"
#include "lights.glsl"
#include "random.glsl"
#include "traversal.glsl"
#include "variants.glsl"

/// What the fog sends toward the eye per steradian, `cosine` off the sun's line.
///
/// Mie, not Henyey-Greenstein: a single lobe cannot do this shape, since real droplets throw a diffraction
/// peak within a degree of the light orders of magnitude above anything one `g` reaches, and still send a
/// sixth of isotropic backwards. Per steradian because the sun arrives from one direction as irradiance and
/// what comes back is that irradiance times this; the sky arrives from every direction and needs no phase
/// function.
float fogPhase(float cosine)
{
    const float peak = exp(-0.0990567 / (FOG_DROPLET - 1.67154));
    const float bulk = exp(-2.20679 / (FOG_DROPLET + 3.91029) - 0.428934);
    const float alpha = exp(3.62489 - 8.29288 / (FOG_DROPLET + 5.52825));
    const float share = exp(-0.599085 / (FOG_DROPLET - 0.641583) - 0.665888);

    // Draine's function is Henyey-Greenstein with a `1 + alpha cos^2` term over what that costs it
    // in normalisation.
    const float draine = henyeyGreenstein(bulk, cosine) * (1.0 + alpha * cosine * cosine)
        / (1.0 + alpha * (1.0 + 2.0 * bulk * bulk) / 3.0);

    return mix(henyeyGreenstein(peak, cosine), draine, share);
}

/// How much fog stands between `position` and the sky along the line `towards` a light. Fog shadows
/// itself, and leaving that out is what makes single scattering white out: light reaching a point deep in a
/// bank crossed the whole bank to get there.
///
/// @param towards unit, from the point toward the light. Each source owes its own slant: at night
///        the sun points down, and a moon standing high crosses far less air than one on the rim.
float fogBeamDepth(vec3 position, vec3 towards)
{
    const float slant = FOG_HEIGHT * frame.mFogLift / max(towards.z, 1.0e-3);
    return frame.mFogExtinction * fogHeightAt(position) * slantCoverage(position, towards, 2.0 * slant) * slant;
}

/// What the two moons put into the air along a ray, before their slant through the fog: each
/// irradiance through the phase function at the ray's own angle to it. In `MoonDisc` order.
struct MoonTerms
{
    vec3 mMasser;
    vec3 mSecunda;
};

/// One of them. The phase is held at the moon's own limb, inside its disc: it takes the moon for a point at
/// the disc's centre, which fits a sun half a degree across and not a moon of twelve and more, whose
/// diffraction peak drew a bright spot on the disc's dark side.
vec3 fogMoonTerm(SkySource moon, vec3 direction)
{
    const float limb = sqrt(max(1.0 - moon.mLimb * moon.mLimb, 0.0));
    return HAS_MOONS ? moon.mIrradiance * fogPhase(min(dot(direction, moon.mDirection), limb)) : vec3(0.0);
}

/// What one froxel is to do about the sky's own lights: whether the sun is up, what the moons put into the
/// air, and which of the pair the one ray goes to.
///
/// Hoisted because a directional source holds its angle to the ray, so its phase function is one
/// evaluation for the whole of it.
struct FogSources
{
    /// Whether there is a sun at all, which an interior and a night both answer no to.
    bool mSunlit;

    /// The moons light the air too: at night the only other light is `mFogColour`, the dome's own colour,
    /// which would make the air around a red moon blue-grey. The pair whole, because a `vec3[2]`
    /// subscripted by the draw is a function-scope array on this hardware and so a read of scratch memory.
    MoonTerms mTerms;

    /// Whether the pair is worth the one ray they share.
    ///
    /// A moon casts a shaft too, and at night it is the only thing that can: air standing behind a headland
    /// gets no moonlight at all. This says whether the pair is worth the one ray; what the pair delivers is
    /// `moonsInAir` either way, so a moon under `FOG_SHAFT_FLOOR` loses its whole contribution, not only its
    /// shadow.
    bool mMoonlit;

    /// What the one the draw landed on puts into the air, and the chance it was drawn.
    ///
    /// Drawn in proportion to what each delivers, as a surface draws them and the lamps are drawn. Masser is
    /// almost always the larger and the brighter, so a second ray for Secunda's shadow would cost as much
    /// again for a light a quarter its size. The sun's own ray is not traced at night, so this spends what
    /// the day already spends.
    vec3 mDrawn;
    float mChance;

    /// The one the draw landed on, so a ray can be aimed at it and its slant through the fog taken.
    /// A `SkySource` and not an index into `frame.mMoons`, so the thing itself is carried instead of two
    /// places deriving it from a number (`moonsInAir`, `fogscatter.rgen`).
    SkySource mDrawnSky;
};

/// What the sun puts into one point of the air, before its own colour and before a phase function. Both
/// are functions of the direction alone, so they factor out of the integral and the trace puts them back at
/// the pixel's own angle; the froxel volume keeps the sun in an image of its own for that reason.
///
/// Nothing here asks the hour: at night `mSun.mDirection` points below the horizon, the floor in
/// `fogBeamDepth` pins it, and the beam comes back as nothing. Nor does it ask what water stands over the
/// point, which keeps this one channel rather than three.
///
/// @param visible what a shadow ray found between the point and the sun.
float sunInAir(vec3 position, float visible)
{
    return visible * exp(-fogBeamDepth(position, frame.mSun.mDirection));
}

/// What the two moons put into one point of the air.
///
/// Each on its own slant and not the sun's, which `fogBeamDepth` is where it matters.
///
/// The one drawn, divided by its chance, where a ray was cast, and both whole where none was: the lamps'
/// estimator and the surface's, unbiased for the pair, with the noise of one draw where a froxel's history
/// is what averages it.
///
/// @param lunar what a shadow ray found between the point and the moon the draw named, which is
///        read only where `FogSources::mMoonlit` said the pair was worth casting it.
vec3 moonsInAir(vec3 position, FogSources sources, float lunar)
{
    if (!sources.mMoonlit)
        return sources.mTerms.mMasser * exp(-fogBeamDepth(position, frame.mMoons[0].mSource.mDirection))
            + sources.mTerms.mSecunda * exp(-fogBeamDepth(position, frame.mMoons[1].mSource.mDirection));

    return sources.mDrawn * exp(-fogBeamDepth(position, sources.mDrawnSky.mDirection))
        * (lunar / sources.mChance);
}

/// The column's half of `FogSources`. A directional source holds its angle to a straight ray, so this is
/// one evaluation for the whole column (every froxel samples the column's ray): `fogdepth.rgen` works it
/// out once and stores it a layer a moon.
///
/// The sun is not one of them: its irradiance and phase are functions of the direction alone, so the trace
/// puts both back at the pixel's own angle, and what the froxel keeps for the sun is a transport.
MoonTerms fogMoonTermsAlong(vec3 direction)
{
    return MoonTerms(fogMoonTerm(skySourceAt(SKY_SOURCE_MASSER), direction),
        fogMoonTerm(skySourceAt(SKY_SOURCE_SECUNDA), direction));
}

/// The froxel's half: whether the pair is worth a ray, and which moon that ray goes to.
///
/// @param draw one number in `[0, 1)`, which picks the moon the pair's ray goes to.
FogSources fogSourcesFrom(MoonTerms terms, float draw)
{
    const float masser = dot(terms.mMasser, LUMINANCE_WEIGHTS);
    const float secunda = dot(terms.mSecunda, LUMINANCE_WEIGHTS);

    const float worthARay = FOG_SHAFT_FLOOR * brightest(frame.mFogColour);

    const WeightedPick pick = pickByWeight(masser, secunda, 0.0, draw);
    const bool drewMasser = pick.mIndex == 0u;

    // **Each flag carries its own constant and not only the terms behind it.** A moon's share folds
    // to nothing without one, but the comparison against a uniform does not fold with it — so the
    // block it guards stays in the kernel, which is the whole of what the constant is for.
    return FogSources(sunUp(), terms, HAS_MOONS && brightest(terms.mMasser + terms.mSecunda) > worthARay,
        drewMasser ? terms.mMasser : terms.mSecunda, pick.mChance,
        drewMasser ? skySourceAt(SKY_SOURCE_MASSER) : skySourceAt(SKY_SOURCE_SECUNDA));
}

/// The ray through a point `inside` the block of pixels one column of the fog volume stands for, from
/// nought to one across the block.
Ray fogColumnRayAt(uvec2 column, vec2 inside)
{
    return rayAt(frame.mEyes.mWorld, (vec2(column) + inside) * float(FOG_VOLUME_SCALE) - 0.5);
}

/// The ray one column of the fog volume samples its air along this frame.
///
/// Stated once, because two passes have to agree about it exactly: `fogdepth.rgen` traces it to find where
/// the column's view of the air ends and `fogscatter.rgen` draws every froxel's sample along it, so a
/// froxel's "short of the surface" is measured along the ray the surface was found on. Through a point
/// drawn inside the block, and a different one every frame, so that over frames the column's froxels
/// cover the block rather than one line through it.
Ray fogColumnRay(uvec2 column)
{
    return fogColumnRayAt(column, unitPair(column, STREAM_FOG_COLUMN));
}

/// What the air holds `depth` of the way through the grid, on the line from one slice's sample to
/// the next — which is what the sampler draws between two texels, and the whole of what `FogSlice`
/// asks of a read between two of them.
FogSlice fogSliceAt(vec2 across, float depth)
{
    // **The level named, because no stage that reads the volume has derivatives to choose one.** GLSL gives
    // an implicit fetch in such a stage the base level, so the other spelling compiles to this same read
    // and leaves the level for the reader to know. The volume has one level, so the base is the level it
    // always meant.
    return unpackFogSlice(textureLod(fogSlice, vec3(across, depth), 0.0),
        textureLod(fogSliceSunward, vec3(across, depth), 0.0).x);
}

/// What the weather's own air takes out of what is behind it, and what it puts in on the way.
///
/// Read out of `Rtx::FogVolume` rather than marched: the field, the sources and the arithmetic are
/// `fogscatter.rgen`'s, but a column of the frustum answers for `FOG_VOLUME_SCALE` squared pixels instead of
/// each paying for its own twenty-four steps and eight sun probes.
///
/// The accumulation up to the last edge passed, then the slice the surface stands in, stepped the way
/// `fogintegrate.comp` stepped it: slice `k` holds everything up to `fogDepth((k + 1) / FOG_VOLUME_SLICES) *
/// FOG_REACH` and the sampler's line between two of those is the wrong shape for the rest (`FogSlice`), so
/// the ray goes on along the same two straight pieces the integrate pass used, cut short at the surface, and
/// a surface exactly on a slice's far edge reads exactly what that slice accumulated.
///
/// The sun is put back here and not stored: `fogVolumeSunward` holds its transport with the irradiance and
/// phase taken off, so the blaze around a low sun keeps the pixel's own angle. `Rtx::FogVolume` says why
/// the moons do not.
vec4 fogVolumeAlong(uvec2 pixel, vec3 direction, float distance)
{
    const vec2 across = fogVolumeAcross(vec2(pixel) + 0.5, frame.mFogColumns);

    const float slices = float(FOG_VOLUME_SLICES);
    const float reach = min(distance, FOG_REACH);
    const float along = fogDepthInverse(reach) * slices;

    // The slice the surface stands in, and how far through it — the reach itself lands in the last
    // slice at the whole of it.
    const uint slice = min(uint(along), FOG_VOLUME_SLICES - 1u);
    const float through = along - float(slice);

    // What the ray had accumulated at that slice's near edge: the eye's nothing for the first, and
    // the texel before for every other — exactly on its centre, so the sampler weighs no neighbour
    // along the depth.
    const float edge = (float(slice) - 0.5) / slices;
    FogColumn air = slice > 0u ? unpackFogColumn(textureLod(fogVolumeAir, vec3(across, edge), 0.0),
                                     textureLod(fogVolumeSunward, vec3(across, edge), 0.0).x)
                               : FogColumn(vec3(0.0), 1.0, 0.0);

    // The near half of the slice, as far as the surface reaches into it; then the far half, likewise.
    // A straight piece's mean is its own middle, which is what each read is taken at.
    const float behind = froxelNear(slice);
    const float middle = froxelMiddle(slice);
    if (through <= 0.5)
    {
        air = fogThrough(air,
            fogSliceAt(across, (float(slice) + 0.5 * through) / slices), reach - behind, frame.mFogExtinction);
    }
    else
    {
        air = fogThrough(air,
            fogSliceAt(across, (float(slice) + 0.25) / slices), middle - behind, frame.mFogExtinction);

        // **Flat where the next slice starts past the column's own surface**, which is the rule the
        // integrate pass carried the same half by: that slice holds none of this column's air, and a line
        // bent toward it thinned the air in the last quarter of every slice a surface stood in.
        const float surface = imageLoad(fogColumnDepth, ivec2(pixel / FOG_VOLUME_SCALE)).x;
        const float onward = froxelNear(slice + 1u) < surface ? 0.25 + 0.5 * through : 0.5;
        air = fogThrough(air,
            fogSliceAt(across, (float(slice) + onward) / slices), reach - middle, frame.mFogExtinction);
    }

    const vec3 sun
        = HAS_SUN ? frame.mSun.mIrradiance * (air.mSunward * fogPhase(dot(direction, frame.mSun.mDirection))) : vec3(0.0);

    return vec4(air.mScattered + sun, air.mTransmittance);
}

/// What a column of air along one ray is, before anything says how far to follow it.
///
/// Split out because the sprite march asks for the same ray at a hundred distances: every term here is a
/// function of the origin and the direction alone. The exponential it hoists is not what it is for, since
/// the compiler already lifts it out of the loop; the split reads the same.
struct FogRay
{
    /// How far the eye stands over the fog's base, which may be under it.
    float mFrom;

    /// How fast that height changes per unit travelled, which is the direction's own `z`.
    float mRise;

    /// The scale height the profile falls off over.
    float mScale;

    /// `exp` of the entry height over that scale, which every span from this origin shares.
    float mEntering;

    /// One where the air reaches the ground, nought where a sea floor cuts it off.
    float mUnder;
};

/// What that column is, for one origin and one direction.
FogRay fogRayFrom(vec3 origin, vec3 direction)
{
    FogRay ray;
    ray.mScale = FOG_HEIGHT * frame.mFogLift;
    ray.mFrom = origin.z - fogBase();
    ray.mRise = direction.z;

    // What a point below the base holds. A dry cell's layer is capped there rather than growing
    // without bound; a wet cell's stops at the surface, because the air pools *at* the water.
    ray.mUnder = fogPools() ? 0.0 : 1.0;

    // Where the ray enters the layer is where it starts, in every case the span below reaches: a ray that
    // starts above the base enters at its own height and one that starts below enters at the base itself,
    // which is a height of nought.
    ray.mEntering = ray.mFrom > 0.0 ? exp(-ray.mFrom / ray.mScale) : 1.0;

    return ray;
}

/// The layer's optical depth over the first `span` of a ray, exactly, before the coverage band.
///
/// The half of the air a closed form reaches: what varies along a ray is a height falloff and a coverage
/// band, and only the first is an exponential in `z`, so its integral is the one every layered atmosphere
/// has a closed form for. A reader multiplies this by `fogCoverageAt` at the path's mean-value point.
float fogColumnOver(FogRay ray, float span)
{
    const float under = ray.mUnder;
    const float scale = ray.mScale;

    const float from = ray.mFrom;
    const float to = from + ray.mRise * span;

    // The stretch spent above the base: the heights it runs between, and its own length. `enters` is `from`
    // or nought and never anything else, which is what `mEntering` was taken from.
    //
    // One expression for the four cases the two signs make, each the bits its own branch gave. Both ends
    // above: `x / x` is one. One end above: `from - 0` is `from`, and `(-to) / (from - to)` is
    // `to / (to - from)` because a negation is exact.
    const float enters = max(from, 0.0);
    const float leaves = max(to, 0.0);
    const float above = from == to ? (from > 0.0 ? span : 0.0) : span * ((enters - leaves) / (from - to));

    // **Both exponentials are taken before the division and neither can overflow**, because both heights
    // are above the base.
    const float entering = ray.mEntering;
    const float leaving = exp(-leaves / scale);
    const float climb = (leaves - enters) / scale;

    // The mean of the falloff over that stretch, `(E - L) / c`, which is the two ends' geometric mean times
    // `sinh(c/2) / (c/2)`.
    const float squared = climb * climb;
    const float shallow = sqrt(entering * leaving) * (1.0 + squared * (1.0 / 24.0 + squared * (1.0 / 1920.0)));
    const float mean = abs(climb) < 0.5 ? shallow : (entering - leaving) / climb;

    return frame.mFogExtinction * (above * mean + under * (span - above));
}

/// The same for a caller that follows one ray to one distance and asks nothing else of it.
float fogColumn(vec3 origin, vec3 direction, float span)
{
    return fogColumnOver(fogRayFrom(origin, direction), span);
}

/// Weighs every lamp reaching a stretch of a ray into `kept`, and returns what they scatter into it.
///
/// One walk of the light grid rather than one per step, which is the cost that made the air expensive in a
/// room: a cell's list is the same over the stretch the ray spends in it, so it is asked once. The air's
/// density and what is left of the ray are read at the lamp's closest approach, held inside the stretch.
///
/// The sum and the reservoir are one estimator: every lamp is offered with the weight of its share of the
/// sum, so the one held is drawn in proportion to its contribution and `sum * lampVisible(kept)` carries the
/// expectation of the whole sum shadowed lamp by lamp, exactly. What is left of the draw is which lamp the
/// one ray goes to.
vec3 lampsInAir(inout Reservoir kept, inout uint state, vec3 origin, vec3 direction, float entry, float exit,
    out float phaseShare)
{
    // **What the lamps send toward the eye, against what they leave the air with**: the medium's phase, as the sun
    // and the moons take it, evaluated per lamp at the middle of its stretch, as a share of the isotropic sum that
    // is returned. One share for the lamps' mix, weighted by each one's light: the sum stays isotropic, because a
    // puff reads what the air holds and lights its card by it (`puffLight`), and the eye reads the share beside it.
    phaseShare = 1.0;
    float phased = 0.0;
    float lit = 0.0;

    // None, for a picture no lamp lights: one answer for the whole frame.
    if (frame.mNoLamps != 0u)
        return vec3(0.0);

    const float side = 1.0 / frame.mLightGrid.mInverseCell;
    const vec3 beyond = frame.mLightGrid.mOrigin + vec3(frame.mLightGrid.mSize) * side;

    vec3 scattered = vec3(0.0);

    // Clipped to the grid before the walk, so the budget above is spent inside it: a ray that starts
    // outside would otherwise cross empty cells until it ran out.
    for (int axis = 0; axis < 3; ++axis)
    {
        if (abs(direction[axis]) < 1.0e-8)
        {
            if (origin[axis] < frame.mLightGrid.mOrigin[axis] || origin[axis] >= beyond[axis])
                return scattered;
            continue;
        }

        const float one = (frame.mLightGrid.mOrigin[axis] - origin[axis]) / direction[axis];
        const float other = (beyond[axis] - origin[axis]) / direction[axis];
        entry = max(entry, min(one, other));
        exit = min(exit, max(one, other));
    }

    if (!(exit > entry))
        return scattered;

    // The cell the ray enters, and for each axis the `t` of its next boundary and the `t` between
    // boundaries after that — a digital differential analyser, so the cell is carried rather than
    // worked out again from a position that would need nudging over each edge.
    vec3 cell = floor((origin + direction * entry - frame.mLightGrid.mOrigin) * frame.mLightGrid.mInverseCell);
    vec3 next = vec3(exit);
    vec3 stride = vec3(0.0);
    const vec3 onward = sign(direction);

    for (int axis = 0; axis < 3; ++axis)
    {
        if (abs(direction[axis]) < 1.0e-8)
            continue;

        const float boundary = frame.mLightGrid.mOrigin[axis] + (cell[axis] + max(onward[axis], 0.0)) * side;
        next[axis] = (boundary - origin[axis]) / direction[axis];
        stride[axis] = side / abs(direction[axis]);
    }

    float behind = entry;
    for (uint visited = 0u; visited < FOG_CELLS_ALONG; ++visited)
    {
        const float leave = min(min(next.x, next.y), next.z);
        const float ahead = min(leave, exit);

        const uvec2 near = lampsWithin(lampsInCell(cell));
        for (uint i = near.x; i < near.y; ++i)
        {
            const uint row = lightListAt(i);
            const GpuLight held = shownLightAt(row);

            // The lamp's distance off the ray from what is left of the offset past its closest
            // approach, and not `|offset|² - closest²`: that difference cancels for a lamp nearly on
            // the ray far down it, by a unit or two at five thousand, which is a source's radius.
            const vec3 offset = held.mPosition - origin;
            const float closest = dot(offset, direction);
            const vec3 across = offset - direction * closest;
            const float perpendicular = sqrt(dot(across, across));

            // **The part of this cell's stretch the lamp reaches at all**, where its chord through
            // the reach and the stretch overlap, measured from the closest approach so the clip is
            // exact; a chord of nought where the lamp stands past its reach off the ray.
            const float halfChord = sqrt(max(held.mReach * held.mReach - perpendicular * perpendicular, 0.0));
            const float low = max(behind - closest, -halfChord);
            const float high = min(ahead - closest, halfChord);

            // A lamp that reaches none of the stretch is passed by, where the surface's walk offers every
            // lamp (`weighLamps`): its integral is nought exactly, and computing it anyway cost the air's
            // zone 2-4 % at the median on a high-end NVIDIA laptop GPU (2026-11-01).
            if (!(high > low))
                continue;

            // The falloff stands in for a mean and is the one thing approximated: a lamp's falloff and the
            // air's density do not correlate over the stretch, the one varying over a lamp's reach and the
            // other over a scale height.
            const float crossed = falloffAlong(perpendicular, low, high, held.mReach, held.mSourceRadius);

            const vec3 share = held.mIntensity * (INV_FOUR_PI * crossed);
            scattered += share;
            considerLamp(kept, state, airCandidate(share), row);

            // Seen from the middle of what the lamp reaches of the stretch: the angle the light turns there to
            // reach the eye, as `fogPhase` takes the sun's, and no nearer the lamp's line than its source's edge,
            // as a moon is held to its limb: a lamp has a size, and the peak a point would throw it does not.
            const vec3 toLamp = held.mPosition - (origin + direction * (closest + 0.5 * (low + high)));
            const float away = length(toLamp);
            const float limb = away / sqrt(away * away + held.mSourceRadius * held.mSourceRadius);
            const float cosine = away > 0.0 ? min(dot(toLamp, direction) / away, limb) : 0.0;
            const float weight = dot(share, LUMINANCE_WEIGHTS);
            phased += weight * (fogPhase(cosine) / INV_FOUR_PI);
            lit += weight;
        }

        if (leave >= exit)
            break;

        const int axis = next.x <= next.y ? (next.x <= next.z ? 0 : 2) : (next.y <= next.z ? 1 : 2);
        cell[axis] += onward[axis];
        next[axis] += stride[axis];
        behind = leave;
    }

    phaseShare = lit > 0.0 ? phased / lit : 1.0;
    return scattered;
}

/// How much of the edge's ramp a ray has crossed by `distance` of range from the eye, as a share of the
/// whole: the integral of `exp(range / FOG_EDGE_RAMP)`, normalised to one where the ground stops.
float fogEdgeCrossed(float distance)
{
    const float range = min(distance, frame.mFogEdge) / frame.mFogEdge;
    return (exp(range / FOG_EDGE_RAMP) - 1.0) / (exp(1.0 / FOG_EDGE_RAMP) - 1.0);
}

/// What the far end of the world takes out of what is behind it, and what it puts in on the way.
///
/// The second element of the air, about this renderer rather than the weather. The ground stops at
/// `mFogEdge` and the ring where it stops is a cut edge in mid-air, which the weather's own extinction
/// cannot close: it is Morrowind's record of the air over that same reach, and clear weather leaves a third
/// of the last cell showing.
///
/// Closed form, because the density is a function of the range from the eye alone (no noise, height or
/// lamps), so the optical depth is its integral and not a march. The range is the ray's own and not its
/// shadow on the ground, as the terrain itself is culled (`Rtx::CellGrid::reachOf`).
///
/// It scatters the sky's own gradient rather than the fog's colour: they are one at the horizon, so nothing
/// is lost near the ring, and above it the gradient is what a ray that reaches nothing already returns, so
/// the world's edge converges onto the sky beside it without flattening its lower half.
///
/// Over the stretch of a ray from `from` to `to` of range from the eye: the eye's own ray from nought, and a
/// leg a surface sent on from the range the eye's ray had already come.
vec4 fogEdgeOver(vec3 direction, float from, float to)
{
    // A room has no edge to hide, and neither has a test that did not ask for one.
    if (!(frame.mFogEdge > 0.0))
        return vec4(0.0, 0.0, 0.0, 1.0);

    // **A climb and not a descent.** Everything above the eye is sky however far off it is, and sky needs
    // no hiding; everything below it is ground, and the ring where that ground stops is the whole reason
    // this is here. An eye on a mountain looks *down* at that ring, so a mask that read the elevation
    // either way would switch the air off in the one place that can see the cut best.
    const float rise = 1.0 - smoothstep(0.0, FOG_EDGE_RISE, max(direction.z, 0.0));
    if (!(rise > 0.0))
        return vec4(0.0, 0.0, 0.0, 1.0);

    // A ray that ends short of the edge is charged for exactly the part of the ramp it crossed.
    const float crossed = fogEdgeCrossed(to) - fogEdgeCrossed(from);

    const float transmittance = pow(FOG_EDGE_TRANSMITTANCE, rise * crossed);
    const vec3 haze = skyGradient(frame.mSkyHorizon, frame.mSkyZenith, frame.mSkyRamp, direction);

    return vec4(haze * (1.0 - transmittance), transmittance);
}

/// What the weather's air lets through along `span` of a ray the eye did not cast — one a surface sent on —
/// and the band's share of it read once, at the stretch's middle: the closed form the cloud's shells and
/// the puffs are charged by, because the froxel volume holds the eye's rays and no other.
float fogThroughLeg(vec3 origin, vec3 direction, float span)
{
    return exp(-fogColumn(origin, direction, span) * slantCoverage(origin, direction, span));
}

/// The air along a ray the eye did not cast, over `span` of it: what a mirror sees across, which the eye's
/// own air in front of the mirror does not cover. What scatters in is the weather's own colour, the ambient
/// half of what the volume holds, and not the sun's or the lamps': that is a question of shadow at every
/// point, which a leg cannot ask of froxels only the eye's rays fill. The world's edge beyond it is over the
/// range this leg adds to the `before` the eye's ray had come, so a mirror shows no more of the ring than the
/// eye's own ray would.
vec4 fogAlongLeg(vec3 origin, vec3 direction, float span, float before)
{
    const float through = fogThroughLeg(origin, direction, span);
    const vec4 weather = vec4(frame.mFogColour * (1.0 - through), through);

    const vec4 edge = fogEdgeOver(direction, before, before + span);

    return vec4(weather.xyz + weather.w * edge.xyz, weather.w * edge.w);
}

/// The air between the eye and everything else: the weather's, and the world's own edge beyond it.
///
/// The edge stands beyond the weather and not in front of it, where its air actually is: its density is
/// nothing until the last quarter of the reach, so what it scatters arrives dimmed by the whole of the
/// weather's air in front of it.
vec4 fogAlong(uvec2 pixel, WorldRay ray, float distance)
{
    // Air only, and an eye under the surface has none in front of it: the water stands in front, and the air
    // a ray reaches past the plane is behind it (`mediumOverColumn` charges it as a leg's air, from where the
    // ray rose through the plane), because `MASK_WATER` stops `fogdepth.rgen`'s column at the surface. Here
    // rather than in each element because only one of the three could tell: the volume accumulates along its
    // column, and the slices past where that column met the surface hold whatever they held when the eye was
    // above it (`fogintegrate.comp` says why it keeps them).
    if (waterOver(ray.mFrom) > 0.0)
        return vec4(0.0, 0.0, 0.0, 1.0);

    // The volume, whatever kind of air this is. A room could read a closed form instead, but that is a lamp
    // reservoir and a ray per pixel, where the volume walks the lamps once per froxel and hands this two
    // fetches, which costs an interior less.
    const vec4 weather = fogVolumeAlong(pixel, ray.mAlong, distance);

    const vec4 edge = fogEdgeOver(ray.mAlong, 0.0, distance);

    return vec4(weather.xyz + weather.w * edge.xyz, weather.w * edge.w);
}

#endif
