#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_LIGHTS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_LIGHTS_GLSL

// Which lamps could reach a point, and what one delivers at a distance.
//
// **The two halves every consumer of a lamp must agree on**: a surface and the air differ in the cosine, the
// shadow ray and the phase function, but not in the reach and the falloff.

#include "colour.h"
#include "gbuffer.h"
#include "look.h"
#include "scene.h"
#include "sky.h"
#include "bindings.glsl"
#include "gloss.glsl"
#include "random.glsl"
#include "traversal.glsl"
#include "variants.glsl"

/// The `source`th light in the sky, read off the frame: `SKY_SOURCE_SUN`, then the two moons.
///
/// **Read and not assembled**: the frame carries the sun as this record and a moon's disc as
/// `MoonDisc::mSource`, so nothing here derives a fact the frame already states.
SkySource skySourceAt(uint source)
{
    if (source == SKY_SOURCE_SUN)
        return frame.mSun;

    return frame.mMoons[source - SKY_SOURCE_MASSER].mSource;
}

/// What the world leaves of a light in the sky at a point, from none of it to all.
///
/// **One question, asked by every reader**, so a surface, a froxel of the air and a step of a water shaft
/// cannot part in their answers. The ray is drawn across the disc's own penumbra: the sun's for the reason
/// `SUN_SHADOW_RADIUS` gives, a moon's at its own limb.
///
/// **The cloud deck stands in no ray's way**: the content dims the sun per weather (`Sun_*_Color`), which is
/// all a deck does to the light under it.
///
/// **Two halves, `Passage`'s**: whether the ray was stopped, and what the translucent surfaces it crossed
/// let through.
///
/// @param step the step off the triangle `position` stands on (`stepOf`), or nought for a point in
///        the air.
/// @param draw one pair in `[0, 1)`, which aims the ray inside the disc's cone.
/// @param nearest whether the occluder must be the nearest (`passageAsk`): a split ray's.
RayAsk skyAsk(SkySource sky, vec3 position, vec3 step, vec2 draw, bool nearest)
{
    // A picture with shadows off is open to the sky everywhere: one answer for the whole frame.
    if (frame.mNoSkyShadows != 0u)
        return noRay();

    const vec3 towards = coneDirection(sky.mDirection, sky.mLimb, draw);
    return skyLightAsk(leaveSurface(position, step, towards), towards, frame.mReach, nearest);
}

/// The same, traced at once and read as one number: the product `throughAnswered` makes of its
/// own halves.
float skyVisible(SkySource sky, vec3 position, vec3 step, vec2 draw)
{
    return throughAnswered(answerAsked(skyAsk(sky, position, step, draw, false)));
}

/// Which lamps one cell of the grid holds, as a range into the light list.
///
/// **By cell rather than by point, because a walk along a ray has the cell already**: the stretch a ray
/// spends in one cell is one list asked once, which `weighLamps` is built on. Walking every lamp of a cell
/// costs the same whether one contributes or none, which the fog's twenty-four asks per pixel made
/// unaffordable.
/// @param key nought for the lamps that light, one for those that take light away: each cell keeps
///        the two runs side by side — `Rtx::LightGrid::getCells`.
uvec2 lightRunInCell(vec3 cell, uint key)
{
    // **Asked as "inside", so a cell that is not a number is outside**: every comparison with a NaN
    // is false, and asked as "outside" one passed on to a conversion with no answer and a read
    // wherever that landed.
    if (!all(greaterThanEqual(cell, vec3(0.0))) || !all(lessThan(cell, vec3(frame.mLightGrid.mSize))))
        return uvec2(0u, 0u);

    const uvec3 at = uvec3(cell);
    const GpuLightCell held
        = lightCellAt(lightGridCell(at.x, at.y, at.z, frame.mLightGrid.mSize.x, frame.mLightGrid.mSize.y));

    return uvec2(held.mFirst[key], held.mFirst[key] + held.mCount[key]);
}

/// The grid cell `position` stands in, which may lie outside the grid.
vec3 lightCellOf(vec3 position)
{
    return floor((position - frame.mLightGrid.mOrigin) * frame.mLightGrid.mInverseCell);
}

uvec2 lampsInCell(vec3 cell)
{
    return lightRunInCell(cell, 0u);
}

/// The lamps that take light away in the cell at `position`.
uvec2 darkeningReaching(vec3 position)
{
    return lightRunInCell(lightCellOf(position), 1u);
}

/// How many lamps one point may weigh before it stops.
///
/// **A budget, and not a limit a healthy grid reaches.** `Rtx::LightGrid` makes a cell coarser rather than
/// the table larger when a lamp's reach is long, so in the limit one cell holds every lamp; the bound keeps a
/// kernel's run time a property of the shader and never of the content. A scene past it loses the lamps its
/// cell listed last: a bias, and only where the alternative is no picture.
const uint LAMPS_AT_A_POINT = 256u;

/// The same range, cut to that budget.
uvec2 lampsWithin(uvec2 near)
{
    return uvec2(near.x, min(near.y, near.x + LAMPS_AT_A_POINT));
}

/// The same, for a caller holding a place instead of a cell.
uvec2 lampsReaching(vec3 position)
{
    return lampsInCell(lightCellOf(position));
}

/// The run of `reaching` a surface walks, cut to the budget: an empty one, selected, for a picture no lamp lights
/// (`FrameConstants::mNoLamps`) or a surface none lights (`Surface::mLampLit`), and the loop is over.
uvec2 lampRunOf(uvec2 reaching, bool lampLit)
{
    return frame.mNoLamps != 0u || !lampLit ? uvec2(0u) : lampsWithin(reaching);
}

/// The size a lamp's singularity is softened by: its own extent, and one unit where it carries less.
///
/// **The lamp's own extent, because that is what a lamp is.** An inverse square is a point's field and runs
/// away within a flame's own radius, drawing a hard bright bead in the fog beside a lamp; a sphere's
/// irradiance flattens inside its own surface instead.
float lampSoftening(float source)
{
    return max(source, 1.0);
}

/// How much of a light `distance` away arrives, per unit intensity.
float falloff(float distance, float reach, float source)
{
    const float ratio = distance / reach;
    const float window = clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    const float held = lampSoftening(source);

    return window * window / (distance * distance + held * held);
}

/// The integral of `falloff` along a ray, over the stretch of it between `from` and `to`.
///
/// **The same window and the same inverse square, integrated instead of sampled**: a froxel's whole share of
/// a lamp, which a march of that stretch converges to. `lampsInAir` is the caller.
///
/// @param perpendicular how far the lamp stands off the ray's line.
/// @param from where the stretch starts, measured from the closest approach and signed: inside the
///        chord the reach cuts from the ray, because past the reach the window is exactly zero and
///        the polynomial that stands for it there is not. The caller clips it (`lampsInAir`).
/// @param to where it ends, likewise, and not before `from`: an empty stretch is nought exactly.
float falloffAlong(float perpendicular, float from, float to, float reach, float source)
{
    const float low = from / reach;
    const float high = to / reach;

    // In units of the reach, where the chord runs from `bump` to one and the guard `falloff` keeps
    // against the singularity is this much of it.
    const float bump = perpendicular * perpendicular / (reach * reach);
    const float held = lampSoftening(source);
    const float guard = held * held / (reach * reach);

    const float c2 = -guard;
    const float c1 = guard * guard - 2.0;
    const float c0 = guard * (2.0 - guard * guard);
    const float rest = (1.0 - guard * guard) * (1.0 - guard * guard);

    // The cubic, with `q = bump + s^2` expanded into powers of `s`. Its leading coefficient is one,
    // which is why there is no `c3` above.
    const float k0 = ((bump + c2) * bump + c1) * bump + c0;
    const float k2 = (3.0 * bump + 2.0 * c2) * bump + c1;
    const float k4 = 3.0 * bump + c2;

    const float root = sqrt(bump + guard);

    const float lowSquared = low * low;
    const float highSquared = high * high;

    const float below = low * (k0 + lowSquared * (k2 / 3.0 + lowSquared * (k4 / 5.0 + lowSquared / 7.0)))
        + rest * atan(low / root) / root;
    const float above = high * (k0 + highSquared * (k2 / 3.0 + highSquared * (k4 / 5.0 + highSquared / 7.0)))
        + rest * atan(high / root) / root;

    // The substitution measured `s` in reaches, and `dt` carries the reach back out.
    return (above - below) / reach;
}

/// One lamp as it arrives at a point.
///
/// **The reach test and the falloff**, the part a surface and the air may not differ in: written once, and
/// each weighs it its own way.
struct Lamp
{
    /// Unit, from the point toward the lamp. Zero where the lamp does not reach.
    vec3 mTowards;

    /// What share of that intensity arrives here, or nothing where the lamp does not reach.
    float mReaching;

    /// How far the lamp's centre is, which a fill's ball is measured against.
    float mDistance;
};

Lamp lampAt(GpuLight lamp, vec3 position)
{
    const vec3 offset = lamp.mPosition - position;
    const float squared = dot(offset, offset);

    // **An early-out and not a rule**: the window in `falloff` is already exactly zero at and beyond the
    // reach, so this changes no pixel. On the square, before the root and the divide, because most of a
    // cell's list fails here.
    if (squared >= lamp.mReach * lamp.mReach || squared <= 0.0)
        return Lamp(vec3(0.0), 0.0, 0.0);

    const float distance = sqrt(squared);

    return Lamp(offset / distance, falloff(distance, lamp.mReach, lamp.mSourceRadius), distance);
}

/// One lamp held out of all the ones that could reach a point, and what it stands for.
///
/// **A reservoir is one candidate and the weight of everything it beat**: the one held is divided by the
/// chance it was held, so a dim lamp that happens to win still speaks for the whole cell.
///
/// **The lamp is named and not copied**: where it stands, how wide it is and how far short to stop are read
/// off its row when the ray is aimed, so a reservoir whose origin moved aims from where it now is.
struct Reservoir
{
    /// Where the ray this buys leaves from — a shading point lifted off its facet (`Surface::mLift`),
    /// or a froxel of the air.
    vec3 mFrom;

    /// What the lamp held would deliver there with nothing in the way: `LightCandidate::mRadiance`.
    vec3 mRadiance;

    /// What the held lamp's light is to a surface's lobe, `LightCandidate::mSpecular` and
    /// `LightCandidate::mFresnel`: kept from the weighing, so the lobe is evaluated once per lamp.
    vec3 mSpecular;
    vec3 mFresnel;

    /// Which lamp, as a row of the light table.
    uint mLamp;

    /// The held lamp's own weight, and the weight of every candidate including it.
    float mWeight;
    float mTotal;

    /// What every lamp `weighLamps` offered would deliver to the diffuse half with nothing in the way,
    /// summed: each candidate's `LightCandidate::mRadiance`, less the share its own lobe took.
    vec3 mUnshadowed;
};

/// A reservoir that has weighed nothing, which buys no ray and delivers nothing.
Reservoir noLamps()
{
    return Reservoir(vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), 0u, 0.0, 0.0, vec3(0.0));
}

/// What a surface's diffuse half needs to take a light: which way it faces, which side a light has to stand
/// on, and what light on its far side is worth.
///
/// **Which side is the interpolated normal's answer** on everything the content did not double: the facets
/// of a solid lean off the surface its normals describe, and read off the plane, whole triangles of a
/// boulder went black under a light its surface plainly faces.
struct Facing
{
    vec3 mNormal;

    /// What decides which side of the surface a light has to stand on — `Surface::mSmooth`, or
    /// `Surface::mGeometric` on a sheet.
    vec3 mSide;

    /// The normal under the map, which a light near its horizon fades the mapped light by
    /// (`bumpShadowing`): `Surface::mSmooth` on a solid. On a sheet the normal itself, which fades
    /// nothing: a sheet's far side is a light's other half and not a terminator.
    vec3 mBase;

    /// What the far side of a sheet is worth, `Surface::mTransmission`. Nought for a solid.
    float mTransmission;
};

Facing facingOf(Surface surface)
{
    return Facing(surface.mNormal, surface.mSheet ? surface.mGeometric : surface.mSmooth,
        surface.mSheet ? surface.mNormal : surface.mSmooth, surface.mTransmission);
}

/// `bumpShadowing` for one light's direction, which a lit cosine and a lobe both take.
///
/// **One exactly where nothing is mapped**, selected and not worked out: a unit normal's square is one to a
/// rounding, so a vanilla surface would move by an ulp it has no reason to. And one in a tuple that maps
/// nothing (`HAS_MAPS`), which the specialization folds the share away under, where the float comparison
/// cannot be folded under the pinning.
float terminatorShare(Facing facing, vec3 towards)
{
    const float share = bumpShadowing(dot(facing.mBase, towards), dot(facing.mBase, facing.mNormal));
    return !HAS_MAPS || all(equal(facing.mBase, facing.mNormal)) ? 1.0 : share;
}

/// The cosine a diffuse surface takes a light at, with what a sheet takes from its far side.
///
/// **One statement of what "facing" means, used by the sun, the moons and every lamp.** A solid takes the
/// near side and nothing from behind; a masked sheet (a leaf) takes the far side at `SHEET_TRANSMISSION` of
/// the near (`MESH_SHEET`), never both at once.
float litCosine(Facing facing, vec3 towards)
{
    const float cosine = dot(facing.mNormal, towards);
    return dot(facing.mSide, towards) > 0.0 ? max(cosine, 0.0) * terminatorShare(facing, towards)
                                            : facing.mTransmission * max(-cosine, 0.0);
}

/// The cosine of the receiver's own plane to a light, either side: what a penumbra laid on it is stretched
/// by (`receiverStretch`).
float planeCosine(Facing facing, vec3 towards)
{
    return abs(dot(facing.mSide, towards));
}

/// Which of three weights one draw picks, and what that pick is worth.
struct WeightedPick
{
    /// 0, 1 or 2, in the order the weights were given.
    uint mIndex;

    /// The picked weight over the total, or one where every weight is nought.
    float mChance;

    /// Whether the picked one holds all of the weight, and is taken whole rather than divided by a
    /// chance of one.
    bool mWhole;

    /// The weights' sum: nought where nothing is worth a ray.
    float mTotal;
};

/// Picks among three weights in proportion to them, with one draw in `[0, 1)`.
///
/// **Against the weights and never against a quotient.** A divide is allowed 2.5 ULP here: `first / first`
/// came back one ulp under one, and the one draw in sixteen million equal to it picked a moon while she was
/// down, a `0 / 0` and a NaN the fog's history spread over the frame. A product is correctly rounded and
/// never exceeds `total`, and a weight of nought is never the pick, so the fallback past every comparison
/// is the last weight that is anything.
WeightedPick pickByWeight(float first, float second, float third, float draw)
{
    const float total = first + second + third;
    const float scaled = draw * total;

    uint index = third > 0.0 ? 2u : (second > 0.0 ? 1u : 0u);
    if (first > 0.0 && scaled < first)
        index = 0u;
    else if (second > 0.0 && scaled < first + second)
        index = 1u;

    const float picked = index == 0u ? first : (index == 1u ? second : third);
    return WeightedPick(index, total > 0.0 ? picked / total : 1.0, !(picked < total), total);
}

/// What one light would deliver to a point with nothing in the way, and the weight it is drawn by.
///
/// **One record for a lamp and a sky source, for a surface and the air**, so what a light is worth cannot
/// be worked out two ways by two askers.
struct LightCandidate
{
    /// What the diffuse half receives, per unit albedo — or, in the air, the phase's share.
    vec3 mRadiance;

    /// What the lobe reflects toward the eye, whole. Nought on a surface with no lobe, and in the air.
    vec3 mSpecular;

    /// The lobe's Fresnel term at this light: the share of `mRadiance` the diffuse half does not get.
    vec3 mFresnel;

    float mWeight;
};

/// A light as a surface weighs it.
///
/// **One target for every light a surface draws between**: a glossy surface weighs a light by all it sends
/// back, the diffuse half net of what the lobe took plus the lobe, so a metal holds the lights its
/// highlights come from.
///
/// @param unshadowed what the diffuse half would receive per unit albedo.
/// @param plain the weight of a surface with no lobe, the luminance of `unshadowed` up to a scale the whole
///        draw shares. **Handed in, because each kind rounds it its own way and the vanilla pictures were
///        drawn with those roundings**: a lamp's is the luminance of the product, a sky source's the cosine
///        times the luminance of the irradiance.
/// @param arriving the light's irradiance square to its direction, which the lobe reflects.
/// @param sine the sine of the light's half angle, which sizes its highlight (`reflectionAt`).
/// @param facing which side a light has to stand on, as `reflectionAt` takes it, and what fades the
///        lobe near the terminator as it fades the diffuse half (`terminatorShare`).
LightCandidate surfaceCandidate(
    vec3 unshadowed, float plain, vec3 arriving, vec3 towards, float sine, Gloss gloss, Facing facing)
{
    LightCandidate candidate = LightCandidate(unshadowed, vec3(0.0), vec3(0.0), plain);
    if (gloss.mGlossy)
    {
        const Reflection reflected = reflectionAt(gloss, facing.mSide, towards, sine);
        candidate.mSpecular = arriving * (reflected.mLobe * terminatorShare(facing, towards));
        candidate.mFresnel = reflected.mFresnel;
        candidate.mWeight = dot(unshadowed * gloss.mDiffuse * (1.0 - reflected.mFresnel) + candidate.mSpecular,
            LUMINANCE_WEIGHTS);
    }

    return candidate;
}

/// A light as a point of the air weighs it: its share, which has no lobe to add.
LightCandidate airCandidate(vec3 share)
{
    return LightCandidate(share, vec3(0.0), vec3(0.0), dot(share, LUMINANCE_WEIGHTS));
}

/// One sky source, weighed for a point that is about to draw between them.
///
/// **Named rather than kept in an array, because a computed index is a spill**: an array with a computed
/// index becomes function-storage locals on this hardware, and three named values cost registers instead.
struct SkyChoice
{
    SkySource mSky;

    /// What the surface makes of this source's direction, or nought where it is not asked.
    float mCosine;

    /// What it would deliver unshadowed, and the weight the draw is made on — `surfaceCandidate`'s,
    /// as a lamp's is.
    LightCandidate mLight;
};

/// Everything about one source that can be known before a ray is traced to it.
///
/// @param asked whether this source is one the caller wants at all — a sun that is down, or a moon
///        a bounce does not ask for.
SkyChoice skyChoiceAt(uint source, Facing facing, bool asked, Gloss gloss)
{
    const SkySource sky = skySourceAt(source);
    const float cosine = asked ? litCosine(facing, sky.mDirection) : 0.0;

    // A source the surface does not face weighs nought and is never drawn, and its lobe is not worth
    // evaluating: in daylight that is both moons.
    LightCandidate light = LightCandidate(vec3(0.0), vec3(0.0), vec3(0.0), 0.0);
    if (cosine > 0.0)
        light = surfaceCandidate(sky.mIrradiance * (cosine * INV_PI), cosine * dot(sky.mIrradiance, LUMINANCE_WEIGHTS),
            sky.mIrradiance, sky.mDirection, source == SKY_SOURCE_SUN ? sin(SUN_ANGULAR_RADIUS) : sky.mLimb, gloss,
            facing);

    return SkyChoice(sky, cosine, light);
}

/// Offers one candidate to `kept`, already resolved to what it delivers where the asker stands. Where the
/// held lamp's ray leaves from is the asker's to set after the walk (`Reservoir::mFrom`).
///
/// **The reservoir's own rule, written once**, because two walks feed it: the point one below, and the walk
/// along a ray that `lampsInAir` takes.
void considerLamp(inout Reservoir kept, inout uint state, LightCandidate candidate, uint lamp)
{
    const float weight = candidate.mWeight;
    if (!(weight > 0.0))
        return;

    kept.mTotal += weight;

    // Hold the newcomer with probability `weight / total`, which leaves each candidate held in
    // proportion to its weight however many follow it — one-deep reservoir sampling.
    if (randomNext(state) * kept.mTotal <= weight)
    {
        kept.mRadiance = candidate.mRadiance;
        kept.mSpecular = candidate.mSpecular;
        kept.mFresnel = candidate.mFresnel;
        kept.mLamp = lamp;
        kept.mWeight = weight;
    }
}

/// The light at `row` of the table, its intensity nought where this frame's view hides every class
/// it answers to: the one read of a lamp every walk makes, so no walk can forget the class.
/// `GpuLight::mTraits` says why a hidden lamp is dark.
GpuLight shownLightAt(uint row)
{
    GpuLight held = lightAt(row);
    held.mIntensity *= lightShown(held.mTraits, frame.mRayMask);
    return held;
}

/// What the lamp at `row` of the table delivers to `from` with nothing in the way, and the weight it is drawn by: a
/// surface's candidate (`surfaceCandidate`), or the air's where `facing` faces nothing.
///
/// **Inside a fill's ball the cosine to the centre is blended out**, by how deep the point stands, because the ball
/// glows on every side of it there.
///
/// **And no test on the reach or the cosine.** A lamp out of reach carries no direction and no share, so its weight
/// is nought, and `considerLamp` refuses a weight of nought; two branches inside a walk of up to `LAMPS_AT_A_POINT`
/// saved one dot and one luminance apiece. The depth is selected and not divided for such a lamp, whose distance is
/// nought over a source that may be a point.
///
/// @param facing the surface's, or a normal of nothing for a point in the air, which has no direction to face
///        away from, so every lamp reaching it counts whole.
/// @param scale what this asker's own share of a lamp is worth: `INV_PI` for a Lambert surface, `INV_FOUR_PI`
///        times a step's weight for the air.
/// @param gloss the surface's specular half, with its diffuse albedo, which weigh a lamp as `surfaceCandidate`
///        says.
LightCandidate lampCandidateAt(uint row, vec3 from, Facing facing, float scale, Gloss gloss)
{
    const GpuLight held = shownLightAt(row);
    const Lamp lamp = lampAt(held, from);

    const float faced = dot(facing.mNormal, facing.mNormal) > 0.0 ? litCosine(facing, lamp.mTowards) : 1.0;
    const float depth = lamp.mReaching > 0.0
        ? lightFill(held.mTraits) * clamp(1.0 - lamp.mDistance / held.mSourceRadius, 0.0, 1.0)
        : 0.0;
    const float cosine = mix(faced, 1.0, depth);
    const vec3 unshadowed = held.mIntensity * (cosine * lamp.mReaching * scale);
    return surfaceCandidate(unshadowed, dot(unshadowed, LUMINANCE_WEIGHTS), held.mIntensity * lamp.mReaching,
        lamp.mTowards,
        held.mSourceRadius > 0.0 ? held.mSourceRadius / max(lamp.mDistance, held.mSourceRadius) : 0.0, gloss, facing);
}

/// What a candidate adds to a reservoir's unshadowed sum: its diffuse half, less the share its own lobe took.
vec3 unshadowedOf(LightCandidate candidate)
{
    return candidate.mRadiance * (1.0 - candidate.mFresnel);
}

/// Weighs every lamp reaching `from` into `kept`, or `candidates` of them drawn from the cell.
///
/// **The surface's walk of the grid, about a point.** The air's — `lampsInAir` — walks the same grid along
/// a ray, and the two feed one rule, `considerLamp`, so what unbiased means cannot come apart between them.
///
/// @param facing,scale,gloss as `lampCandidateAt` takes them.
/// @param lampLit whether the lamps light the surface at all — `Surface::mLampLit`.
/// @param candidates how many lamps to draw where the cell holds more, and nought for every one.
void weighLamps(inout Reservoir kept, inout uint state, vec3 from, Facing facing, float scale, Gloss gloss,
    bool lampLit, uint candidates)
{
    const uvec2 near = lampRunOf(lampsReaching(from), lampLit);

    // **A fixed count of candidates where the run is longer than it**, drawn uniformly and resampled (RIS,
    // Talbot 2005; Bitterli et al. 2020): each is offered at its own weight, and the total and the
    // unshadowed sum are scaled by `n / M` after, which is the estimate's `1 / (M p)` for a pdf of `1 / n`.
    const uint count = near.y - near.x;
    const bool sampled = candidates > 0u && count > candidates;
    const uint steps = sampled ? candidates : count;
    for (uint step = 0u; step < steps; ++step)
    {
        const uint i = near.x + (sampled ? min(uint(randomNext(state) * float(count)), count - 1u) : step);
        const uint row = lightListAt(i);
        const LightCandidate candidate = lampCandidateAt(row, from, facing, scale, gloss);

        kept.mUnshadowed += unshadowedOf(candidate);
        considerLamp(kept, state, candidate, row);
    }

    const float spread = sampled ? float(count) / float(steps) : 1.0;
    kept.mTotal *= spread;
    kept.mUnshadowed *= spread;
}

/// The lamps a pixel's tile saw last frame, which its draw steers to: `LAMP_LIST_ROWS` rows of the light table two
/// to a word, `LAMP_LIST_SLOT_NONE` past the last, or none.
struct LampList
{
    uvec4 mRows;
};

LampList noLampList()
{
    return LampList(uvec4(LAMP_LIST_SLOT_NONE | (LAMP_LIST_SLOT_NONE << 16u)));
}

/// The list `pixel`'s draw steers by: the frame before's at the tile its surface stood in then, `moved` being where it
/// stood less where it stands (`CHANNEL_MOTION`), picked among the four tiles nearest that place by their bilinear
/// weights, an axis a draw (MegaLights' "stochastic bilinear visible light list lookup"), so a tile's edge is no
/// seam where the draw changes lists. None where the frame before's lists stand for nothing here
/// (`VisibilityConstants::mLampListsHeld`), or the place stood off the frame.
LampList lampListOf(uvec2 pixel, vec2 moved)
{
    const uvec2 extent = uvec2(frame.mEyes.mWorld.mWidth, frame.mEyes.mWorld.mHeight);
    const vec2 at = vec2(pixel) + 0.5 + frame.mEyes.mWorld.mJitter + moved;

    // Asked as "inside", so a place that is not a number is outside, as `lightRunInCell` asks.
    if (frame.mLampListsHeld == 0u || !all(greaterThanEqual(at, vec2(0.0))) || !all(lessThan(at, vec2(extent))))
        return noLampList();

    const uvec2 tiles = (extent + LAMP_LIST_TILE - 1u) / LAMP_LIST_TILE;
    const vec2 tile = at / float(LAMP_LIST_TILE) - 0.5;
    const vec2 low = floor(tile);

    uint state = randomSeed(pixelKey(pixel) + SEED_LAMP_LIST_PICK);
    const vec2 draw = vec2(randomNext(state), randomNext(state));
    const ivec2 picked = clamp(ivec2(low) + ivec2(lessThan(draw, tile - low)), ivec2(0), ivec2(tiles) - 1);
    const uint index = uint(picked.y) * tiles.x + uint(picked.x);
    return LampList(lampListAt(index));
}

/// Whether the lamp at `row` is on `list`. A row no lamp of the table has never is, so a list a changed table left
/// behind only steers the draw; and a row no slot can name never is either.
bool listedIn(LampList list, uint row)
{
    return row < LAMP_LIST_SLOT_NONE
        && (any(equal(list.mRows & LAMP_LIST_SLOT_NONE, uvec4(row))) || any(equal(list.mRows >> 16u, uvec4(row))));
}

/// Weighs every lamp reaching `from`, into `listed` where it is on `list` and into `rest` where it is not: **the
/// lamps a pixel draws its two rays between, a stratum each** (`docs/architecture.md`, "The lamps' draw is two
/// strata"). Every lamp is weighed once and offered to the one reservoir it belongs to, so each reservoir's sum is
/// its stratum's exact unshadowed light, and what one draws is independent of the other's.
///
/// **Offered to both, at its weight in one and nought in the other**: `considerLamp` refuses a weight of nought
/// before it draws, so a lamp steps the sequence once whichever stratum it is in, and the walk has one path. With
/// no list every lamp is the rest's, and the rest is `weighLamps`' reservoir of every lamp to the bit.
///
/// @param facing,scale,gloss,lampLit as `weighLamps` takes them; every lamp, and never a fixed count, since each
///        stratum's sum must be exact.
void weighLampStrata(inout Reservoir listed, inout Reservoir rest, inout uint state, vec3 from, Facing facing,
    float scale, Gloss gloss, bool lampLit, LampList list)
{
    const uvec2 near = lampRunOf(lampsReaching(from), lampLit);
    for (uint i = near.x; i < near.y; ++i)
    {
        const uint row = lightListAt(i);
        const LightCandidate candidate = lampCandidateAt(row, from, facing, scale, gloss);
        const float on = listedIn(list, row) ? 1.0 : 0.0;

        LightCandidate onList = candidate;
        onList.mWeight *= on;
        LightCandidate offList = candidate;
        offList.mWeight *= 1.0 - on;

        listed.mUnshadowed += unshadowedOf(candidate) * on;
        rest.mUnshadowed += unshadowedOf(candidate) * (1.0 - on);
        considerLamp(listed, state, onList, row);
        considerLamp(rest, state, offList, row);
    }
}

/// What every lamp that takes light away takes from a surface's direct lamp term, per unit albedo: its
/// unshadowed share, as the rasterizer subtracts a negative light — no shadow ray, because the rasterizer
/// casts none from a point light, and nothing from the far side of a solid.
///
/// @param scale the asker's own share of a lamp, as `weighLamps` takes it: `INV_PI` for a surface.
/// @param lampLit whether the lamps light the surface at all, as `weighLamps` takes it.
vec3 darkeningAt(vec3 from, Facing facing, float scale, bool lampLit)
{
    vec3 taken = vec3(0.0);
    const uvec2 near = lampRunOf(darkeningReaching(from), lampLit);
    for (uint i = near.x; i < near.y; ++i)
    {
        const GpuLight held = shownLightAt(lightListAt(i));
        const Lamp lamp = lampAt(held, from);
        taken -= held.mIntensity * (litCosine(facing, lamp.mTowards) * lamp.mReaching * scale);
    }

    return taken;
}

/// The ray `lampAsk` asks, of the lamp at `row` of the table, leaving from `from`: for an asker that keeps the row
/// and not the reservoir (`heldLampRay`).
RayAsk lampRowAsk(uint row, vec3 from, vec3 step, vec2 draw, bool nearest)
{
    // Aimed from where the ray leaves and not from where the lamp was weighed, with no reach test:
    // a caller that moved its origin after weighing — a lifted surface and the air both do — still
    // aims at the lamp it held.
    const GpuLight lamp = lightAt(row);
    const vec3 offset = lamp.mPosition - from;
    const float distance = length(offset);
    if (!(distance > 0.0))
        return noRay();

    const vec3 axis = offset / distance;
    const vec3 towards = coneDirection(axis, min(lamp.mSourceRadius / distance, 1.0), draw);

    // How far along this direction the source stands beside it, which is where the ray is closest to
    // the lamp and so where the clearance has to be measured from.
    const float along = distance * dot(towards, axis);

    return lampLightAsk(
        leaveSurface(from, step, towards), towards, along - max(lamp.mClearance, SHADOW_BIAS), nearest);
}

/// What the world leaves of the lamp a reservoir held, in `Passage`'s two halves.
///
/// **The one ray**, aimed somewhere on the lamp, stopping at its closest approach to it less the clearance
/// and not at the centre: aimed off-axis, a ray stopped at the centre's distance runs *past* the source. A
/// record's lamp keeps no clearance (`Rtx::makeLight` says why), so its ray runs to the flame, and the facing
/// keeps a lantern's frame or a candle's holder from shadowing it: the ray culls the faces turned toward
/// where it left (`lampLightAsk`).
///
/// **The clearance and the size are two numbers because they answer two questions**: how far short of the
/// lamp nothing shadows it, and how wide the source a penumbra opens to.
///
/// @param step the step off the triangle the reservoir's point stands on (`stepOf`), or nought for a
///        point in the air.
RayAsk lampAsk(Reservoir kept, vec3 step, vec2 draw, bool nearest)
{
    return lampRowAsk(kept.mLamp, kept.mFrom, step, draw, nearest);
}

/// The tangent of a half angle given as its sine (`SkySource::mLimb`): a penumbra's radius over its
/// occluder's distance.
float tangentOfLimb(float limb)
{
    return limb / sqrt(max(1.0 - limb * limb, 1e-12));
}

/// How wide the penumbra stands where a ray to a source was stopped `occluder` along it, as its radius in
/// world units, or `SHADOW_PENUMBRA_CLEAR` where nothing stopped it.
float skyPenumbra(SkySource sky, float occluder)
{
    return occluder < SHADOW_PENUMBRA_CLEAR ? occluder * tangentOfLimb(sky.mLimb) : SHADOW_PENUMBRA_CLEAR;
}

/// The same for a lamp `radius` across at `distance`, `radius occluder / (distance - occluder)`.
float lampPenumbra(float radius, float distance, float occluder)
{
    return occluder < SHADOW_PENUMBRA_CLEAR ? radius * occluder / max(distance - occluder, SHADOW_BIAS)
                                            : SHADOW_PENUMBRA_CLEAR;
}

/// `lampAsk`'s two halves, now and as one number, from none of the lamp to all: `throughAnswered`'s
/// product, exact for the reason it gives.
float lampVisible(Reservoir kept, vec3 step, vec2 draw)
{
    return throughAnswered(answerAsked(lampAsk(kept, step, draw, false)));
}

/// What the lamp a reservoir held is worth to the estimate of every lamp it stands for: the
/// reservoir's weight over the held one's. Nought where nothing was held, which buys no ray.
float heldShare(Reservoir kept)
{
    return kept.mWeight > 0.0 ? kept.mTotal / kept.mWeight : 0.0;
}

#endif
