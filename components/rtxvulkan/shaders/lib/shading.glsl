#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHADING_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHADING_GLSL

// What an ordinary lit surface does with light: the direct sources it can ask about, the
// one bounce it traces for everything else, and the terms the filter demodulates by.

#include "brdf.h"
#include "camera.h"
#include "colour.h"
#include "look.h"
#include "scene.h"
#include "bindings.glsl"
#include "census.glsl"
#include "frame.glsl"
#include "gloss.glsl"
#include "lights.glsl"
#include "random.glsl"
#include "records.glsl"
#include "shadowed.glsl"
#include "sharedexponent.glsl"
#include "sky.glsl"
#include "traversal.glsl"
#include "underwater.glsl"

/// Which end of a path a surface is being shaded at.
///
/// **Not a count of bounces.** What separates them is whether the result is looked at: a seabed through
/// water and a face in a mirror are each a bounce out and each is what the pixel shows, so they are lit
/// like anything else the eye can see — and so is what a glossy surface's lobe reflects, which is the whole
/// of what a metal shows. Only the diffuse hemisphere's far hit is a term that nothing resolves on its own,
/// and only there is a light worth dropping.
const uint PATH_SEEN = 0u;
const uint PATH_INDIRECT = 1u;

/// A penumbra in the surface's own footprints, `footprint` world units each, from its radius in
/// world units: `SHADOW_PENUMBRA_DRAWN` where the bit was drawn, and `SHADOW_PENUMBRA_CLEAR` where no
/// ray was stopped.
float penumbraIn(bool drawn, float radius, float footprint)
{
    return drawn ? SHADOW_PENUMBRA_DRAWN
        : radius < SHADOW_PENUMBRA_CLEAR ? min(radius / max(footprint, 1e-6), SHADOW_PENUMBRA_CLEAR)
                                         : SHADOW_PENUMBRA_CLEAR;
}

/// Whether a shadow bit whose source carries `carried` of its field's `total` is drawn among sources: one
/// that carries less than all but `mShadowFloor` of the field's light is noise however hard its shadow, and
/// `penumbraIn` gives it the whole reach.
///
/// **Asked before its ray as well as after**, because a drawn bit's penumbra is the same whatever stopped
/// the ray: its ray needs no nearest occluder, and stops at the first (`passageAsk`).
bool drawnAmong(float carried, float total)
{
    return carried < (1.0 - frame.mShadowFloor) * total;
}

/// What a penumbra measured square to its light is on a receiver whose plane the light meets at `cosine`
/// (`planeCosine`): `1 / cosine` longer along the light's azimuth, held to `SHADOW_PENUMBRA_STRETCH`.
float receiverStretch(float cosine)
{
    return 1.0 / max(cosine, 1.0 / SHADOW_PENUMBRA_STRETCH);
}

/// `weight` where it is at least `minor`, and nought where it is under it: what a source draws by
/// where a source under the floor is never drawn (`VisibilityConstants::mShadowFloor`).
float drawable(float weight, float minor)
{
    return weight >= minor ? weight : 0.0;
}

/// One where a source has a weight and it is under the floor, and nought otherwise: the minor
/// sources whose light rides the drawn one's ray whole.
float minorWeight(float weight, float minor)
{
    return weight > 0.0 && weight < minor ? 1.0 : 0.0;
}

/// What one sky source delivers to a surface with nothing in the way: the diffuse half per unit
/// albedo, the share of that the lobe takes, and what the lobe reflects, all through the water over
/// the point.
struct SkyTerm
{
    vec3 mDiffuse;
    vec3 mTaken;
    vec3 mSpecular;
};

/// `choice`'s term, and nought where it weighs nothing: in daylight both moons, which then cost no
/// water column.
SkyTerm skyTermOf(SkyChoice choice, vec3 position, float footprint, Gloss gloss)
{
    if (!(choice.mLight.mWeight > 0.0))
        return SkyTerm(vec3(0.0), vec3(0.0), vec3(0.0));

    const vec3 water = lightThroughWater(
        bentPathAt(position, sunUnderWater(choice.mSky.mDirection)), footprint,
        waterCrossingOf(choice.mSky.mDirection).mInto);
    const vec3 diffuse = choice.mSky.mIrradiance * water * (choice.mCosine * INV_PI);
    return SkyTerm(diffuse, gloss.mGlossy ? diffuse * choice.mLight.mFresnel : vec3(0.0),
        gloss.mGlossy ? water * choice.mLight.mSpecular : vec3(0.0));
}

/// `term` times `scale`.
SkyTerm scaledTerm(SkyTerm term, float scale)
{
    return SkyTerm(term.mDiffuse * scale, term.mTaken * scale, term.mSpecular * scale);
}

/// `first` and `scale` of `second`.
SkyTerm joinedTerms(SkyTerm first, SkyTerm second, float scale)
{
    return SkyTerm(first.mDiffuse + second.mDiffuse * scale, first.mTaken + second.mTaken * scale,
        first.mSpecular + second.mSpecular * scale);
}

/// The *direct* light arriving at a point and turning back out of it: per unit albedo for the diffuse half,
/// and whole for the specular.
///
/// **Sources that can be asked where they are, and nothing else.** The sun and the lamps are each a known
/// direction and a shadow ray; everything that arrives by having bounced off something is the bounce's
/// (`BounceRun`), and adding a fill here as well would count that half twice.
///
/// **The lobe is taken at the light the diffuse half drew, toward that light's centre**, and its estimate
/// divides by the same chances the diffuse one does.
///
/// **Everything but the rays' answers**, worked out before either ray (`startGather`): the sky's passage
/// (`SkyPassageRun`) and the held lamp's ray (`gatherLampAsk`) are the caller's to trace, and the caller's
/// to finish the light with (`pathEndLightOf`, `solidLightOf`).
struct GatherRun
{
    /// The pixel's own key, `pixelKey`, which every sequence here is drawn from with a `SEED_`
    /// added: `mLamps`, and `SEED_INDIRECT_LIGHT` for the bounce's rate.
    uint mKey;

    /// Which draw sequence the lamp reservoir steps. **One per depth of the path**, because a bounce
    /// shades a second surface and two reservoirs stepping one sequence would keep correlated lamps
    /// at both ends of it.
    uint mLamps;

    /// `PATH_SEEN` or `PATH_INDIRECT`. It decides whether the moons are asked at all, and whether
    /// the rest of this is drawn at `INDIRECT_LIGHT_RATE` or spent on every hit.
    uint mPath;

    /// Whether the sky's source and the lamps' diffuse half are handed back apart, `mSkyDiffuse` and the
    /// reservoir's unshadowed sum and what is beside each, for the shadow denoiser (`CHANNEL_SHADOWED`,
    /// `CHANNEL_LAMPED`): the solid a pixel shows splits — the eye's own, or what a water path found — and
    /// the pane and the bounce compose.
    bool mSplit;

    /// Whether the shadow rays' draws come from the tile at the pixel (`STREAM_SUN_DISC`): the eye's
    /// own split hit's.
    bool mBlue;

    /// The sky's sources as the start weighed them: unsplit, their estimate under the one ray; split, every
    /// source's light as though the ray got through — the diffuse half per unit albedo and net of the
    /// lobe's share, and the lobe whole — the drawn source, and how far a penumbra on this surface
    /// stretches toward it.
    SkyTerm mSkyEstimate;
    vec3 mSkyDiffuse;
    vec3 mSkySpecular;
    SkySource mSky;
    float mSkyStretch;
    float mSkyCarried;
    float mSkyWeight;

    /// The lamp the reservoir held, the draw its ray takes on the source (`gatherLampAsk`), what the
    /// darkening lamps take off, and how far a penumbra on this surface stretches toward the held
    /// lamp.
    Reservoir mKept;
    vec2 mLampDraw;
    vec3 mDarkening;
    float mLampStretch;

    float mFootprint;
};

GatherRun gatherRunOf(uint key, uint lamps, uint path, bool split, bool blue)
{
    GatherRun run;
    run.mKey = key;
    run.mLamps = lamps;
    run.mPath = path;
    run.mSplit = split;
    run.mBlue = blue;
    run.mSkyEstimate = SkyTerm(vec3(0.0), vec3(0.0), vec3(0.0));
    run.mSkyDiffuse = vec3(0.0);
    run.mSkySpecular = vec3(0.0);
    run.mSky = frame.mSun;
    run.mSkyStretch = 1.0;
    run.mSkyCarried = 0.0;
    run.mSkyWeight = 0.0;
    run.mKept = noLamps();
    run.mLampDraw = vec2(0.0);
    run.mDarkening = vec3(0.0);
    run.mLampStretch = 1.0;
    run.mFootprint = 0.0;
    return run;
}

/// What the start of a gather works out of `surface` and asks of its sky: everything but the two rays'
/// answers.
///
/// @return whether the sky's ray was asked, `skyPassage` started for it.
bool startGather(inout GatherRun run, inout SkyPassageRun skyPassage, Surface surface, Gloss gloss, uvec2 pixel)
{
    const bool split = run.mSplit;
    const vec3 position = surface.mPosition;
    const Facing facing = facingOf(surface);

    // Only the shadow rays leave from the surface the normals describe (`Surface::mLift`): what a
    // light delivers is weighed at the point shaded, the one the eye sees.
    const vec3 leaving = position + surface.mLift;

    uint state = randomSeed(run.mKey + run.mLamps);

    // **Where on a source a shadow ray leaves from is drawn before anything is weighed**, so that it does
    // not depend on how many lamps the cell happened to hold: the two pairs sit at a fixed place in the
    // sequence and the reservoir's own draws follow them.
    //
    // **The eye's own split hit takes the same three from the tile** (`STREAM_SUN_DISC`), and the sequence
    // steps past the hashed ones all the same, so the reservoir's draws stay where they were.
    const vec2 sunHashed = vec2(randomNext(state), randomNext(state));
    const float skyHashed = randomNext(state);
    const vec2 lampHashed = vec2(randomNext(state), randomNext(state));
    const vec2 sunDraw = run.mBlue ? unitPair(pixel, STREAM_SUN_DISC) : sunHashed;
    const float skyPick = run.mBlue ? randomAt(pixel, STREAM_SKY_PICK) : skyHashed;
    const vec2 lampDraw = run.mBlue ? unitPair(pixel, STREAM_LAMP_DISC) : lampHashed;

    // **The sky's sources are weighed and drawn the way the lamps are.** What each would deliver unshadowed
    // is its weight — its cosine and its irradiance, which is everything about it that can be known without
    // tracing — one is drawn in proportion, one ray goes to it, and its share is divided by the draw. In
    // daylight the moons weigh nothing and the sun is always the draw; at night the sun weighs nothing and
    // the draw is between the moons; only the hour either side of dusk spends one ray on two sources, and
    // the accumulator carries the noise that buys.
    //
    // **The disc is sampled for visibility and not for radiometry**, and that is the sharper of the two
    // estimators rather than a saving.
    //
    // **The moons are asked only where the eye can see the surface.** A bounce's far hit is an indirect
    // term nothing resolves on its own, so a moon reaching it through a shadow ray of its own was the
    // dimmest half of the dimmest thing in the frame.
    //
    // **The pick is `pickByWeight`'s**, which says why it is made against the weights.
    const bool lunar = HAS_MOONS && run.mPath == PATH_SEEN;
    const SkyChoice sun = skyChoiceAt(SKY_SOURCE_SUN, facing, sunUp(), gloss);
    const SkyChoice masser = skyChoiceAt(SKY_SOURCE_MASSER, facing, lunar, gloss);
    const SkyChoice secunda = skyChoiceAt(SKY_SOURCE_SECUNDA, facing, lunar, gloss);

    // **A source under `mShadowFloor` of the sky's weight is never the one drawn** (decision 3): its light
    // is in the sum below, and it takes the drawn source's ray.
    const float skyWeight = sun.mLight.mWeight + masser.mLight.mWeight + secunda.mLight.mWeight;
    const float minor = frame.mShadowFloor * skyWeight;
    const WeightedPick pick = pickByWeight(drawable(sun.mLight.mWeight, minor), drawable(masser.mLight.mWeight, minor),
        drawable(secunda.mLight.mWeight, minor), skyPick);

    // What the bit's source carries of the sky's light, as the pair `skyCarried / skyWeight`: its
    // weight over every source's, the minor ones counted against it.
    run.mSkyCarried = 0.0;
    run.mSkyWeight = skyWeight;
    const bool skyAsked = pick.mTotal > 0.0;
    if (skyAsked)
    {
        const SkyChoice picked = pick.mIndex == 0u ? sun : (pick.mIndex == 1u ? masser : secunda);
        run.mSkyCarried = picked.mLight.mWeight;

        // Split, the rays' own bit is handed back and the rest of the estimate is made as though they got
        // through: the product of the two is the estimate unsplit, and the bit is what a denoiser filters
        // in its place.
        const SunUnderWater bent = sunUnderWater(picked.mSky.mDirection);
        skyPassage = skyPassageRunOf(picked.mSky, leaving, stepOf(surface), bentPathAt(leaving, bent), sunDraw,
            split && !drawnAmong(picked.mLight.mWeight, skyWeight));

        // **Each source's term whole, as the lamps' unshadowed sum is**: split, every source with a weight
        // is in the light the bit multiplies, so a pixel's hue is the sources' and not the pick's; unsplit,
        // the drawn one over its chance and the minor ones whole, under the one ray.
        const SkyTerm sunTerm = skyTermOf(sun, position, surface.mFootprint, gloss);
        const SkyTerm masserTerm = skyTermOf(masser, position, surface.mFootprint, gloss);
        const SkyTerm secundaTerm = skyTermOf(secunda, position, surface.mFootprint, gloss);
        const SkyTerm pickedTerm = pick.mIndex == 0u ? sunTerm : (pick.mIndex == 1u ? masserTerm : secundaTerm);

        if (split)
        {
            const SkyTerm every = joinedTerms(joinedTerms(sunTerm, masserTerm, 1.0), secundaTerm, 1.0);
            run.mSkyDiffuse = every.mDiffuse - every.mTaken;
            run.mSkySpecular = every.mSpecular;
            run.mSky = picked.mSky;
            run.mSkyStretch = receiverStretch(planeCosine(facing, picked.mSky.mDirection));
        }
        else
        {
            const SkyTerm minors = joinedTerms(
                joinedTerms(scaledTerm(sunTerm, minorWeight(sun.mLight.mWeight, minor)), masserTerm,
                    minorWeight(masser.mLight.mWeight, minor)),
                secundaTerm, minorWeight(secunda.mLight.mWeight, minor));
            run.mSkyEstimate = joinedTerms(minors, pickedTerm, pick.mWhole ? 1.0 : 1.0 / pick.mChance);
        }
    }

    // A lamp loses nothing to the water, where the sun and the sky both lose the column above the point: it
    // is usually standing in the same water as what it lights, and the depth over the two of them is not
    // between them.
    //
    // **With one lamp in the cell it is exactly the arithmetic that was here before**: the sum is that
    // lamp's weight, the ratio is one, and what is left is the term that was always there.
    //
    // **The lobe takes the held lamp's own lobe, kept from the weighing**, and the reservoir's share: the
    // estimate of the lobe over every lamp is then the one the diffuse half makes, with the lobe where the
    // cosine was.
    Reservoir kept = noLamps();
    // **Every lamp at a split hit, whose unshadowed sum must be exact, and a fixed count of candidates
    // everywhere else** (`VisibilityConstants::mLampCandidates`): a path's far end and a pane cost what a
    // lamp-dense cell costs only where it is seen.
    weighLamps(kept, state, position, facing, INV_PI, gloss, surface.mLampLit, split ? 0u : frame.mLampCandidates);
    kept.mFrom = leaving;
    run.mKept = kept;
    run.mLampDraw = lampDraw;

    // **The lamps that take light away take it off the lamps' exact sum and no further**, floored at nought
    // as the rasterizer clamps its lighting: the sun, the sky and the bounce stay whole.
    run.mDarkening = darkeningAt(position, facing, INV_PI, surface.mLampLit && kept.mWeight > 0.0);
    run.mLampStretch = split && kept.mWeight > 0.0
        ? receiverStretch(planeCosine(facing, normalize(lightAt(kept.mLamp).mPosition - position)))
        : 1.0;
    run.mFootprint = surface.mFootprint;
    return skyAsked;
}

/// The held lamp's shadow ray, or none where no lamp was held.
RayAsk gatherLampAsk(GatherRun run, Surface surface)
{
    return run.mKept.mWeight > 0.0
        ? lampAsk(run.mKept, stepOf(surface), run.mLampDraw, run.mSplit && !drawnAmong(run.mKept.mWeight, run.mKept.mTotal))
        : noRay();
}

/// Whether a gather a path's far end makes, keyed `key` and drawn from the sequence `seed`
/// (`BounceSeeds::mIndirect`), is drawn to run where the sky lights (`INDIRECT_LIGHT_RATE`): asked by
/// the gather and by the stage that lists its landing (`appendLandings`).
bool indirectGatherDrawn(uint key, uint seed)
{
    uint rate = randomSeed(key + seed);
    return randomNext(rate) < INDIRECT_LIGHT_RATE;
}

/// What terminates a path: the cell's own ambient, dimmed by whatever stands over the point.
///
/// **A stand-in for every bounce that is not being traced**, which is what it always was — the difference
/// is that it is now one level down rather than added on top of the one that is.
///
/// @param reaching how much of the ambient the point can see, out of `ambientReaching`. It bites on
///        the whole of it and on both sides of a door: what a room changes is how far the ray looked
///        for the occluder, not whether the answer applies. One is what an asker with no hemisphere
///        to trace hands over.
vec3 pathEnd(vec3 position, float reaching)
{
    return frame.mAmbient * (daylightReaching(position) * reaching);
}

/// `specularModulation`'s shared-exponent word, before it is unpacked: what a stage hands another
/// that divides by the same number (`BounceRun::mModulationWord`).
uint lobeModulationWord(Gloss gloss)
{
    return packRgb9e5(max(gloss.mAlbedo, vec3(SPECULAR_ALBEDO_FLOOR)));
}

/// What the lobe's light is divided by before the glossy filter averages it, and the composite multiplies
/// back: `gloss`'s split-sum specular albedo (`Gloss::mAlbedo`).
///
/// **Rounded to what the answer packs before anything is divided by it** (`packRgb9e5`, whose every value a
/// half holds as well): the light divided by this and the channel the composite multiplies by are one
/// number, so the two meet to the rounding of a product.
vec3 specularModulation(Gloss gloss)
{
    return mix(vec3(1.0), unpackRgb9e5(lobeModulationWord(gloss)), bvec3(gloss.mGlossy));
}

/// What a surface is in the filter's and the composite's terms: its shading normal, its two
/// albedos, and what its lobe's light is multiplied by: `specularModulation`, or one where the lobe's
/// light is not taken apart.
SurfaceResponse responseOf(Surface surface, vec3 specular)
{
    // Counted here, because its code is a whole number whatever the normal was.
    countNotFinite(surface.mNormal);
    return SurfaceResponse(packSurfaceNormal(surface.mNormal), surface.mAlbedo, surface.mAmbientAlbedo, specular);
}

/// `albedo`, one of `surface`'s, as it reflects indirect light under the occlusion its specular map paints
/// (`indirectVisibility`). A surface with no such map keeps the albedo to the bit, and a program with no maps
/// compiles this out.
vec3 occludedAlbedo(Surface surface, vec3 albedo)
{
    if (!HAS_MAPS)
        return albedo;

    const float open = surface.mOcclusion;
    return albedo
        * vec3(indirectVisibility(open, albedo.x), indirectVisibility(open, albedo.y),
            indirectVisibility(open, albedo.z));
}

/// What Night-Eye's lift adds to a surface, per unit of lift, in display values: its ambient albedo
/// encoded, as the rasterizer adds `texture × A × lift` to every fragment it lights — in the values
/// it displays, with nothing to occlude it and no exposure to adapt it away. `CHANNEL_LIFT`.
vec3 liftOf(Surface surface)
{
    return encodeSrgb(surface.mAmbientAlbedo);
}

/// What an ordinary lit surface sends back along the ray that found it. **One statement of what a diffuse
/// surface does with light, used at every depth** — writing it twice is how two would come to disagree.
///
/// @param diffuse what reaches the diffuse half, per unit albedo: the lights `GatherRun` found and what
///        arrives from everything that is not a light — `pathEnd` at the hit a hemisphere found — or
///        whichever part of that the caller has not handed to a filter.
/// @param specular what the lobe reflects toward the eye, whole.
vec3 litSurface(Surface surface, vec3 diffuse, vec3 specular)
{
    // The emissive colour joins the light rather than the albedo, which is where the original engine puts
    // it: it sums the term with the diffuse and ambient light and multiplies the whole by the texture
    // (`files/shaders/compatibility/objects.frag:232`).
    return surface.mAlbedo * (diffuse + surface.mEmissiveColour * EMISSIVE_INTENSITY) + surface.mEmitted + specular;
}

/// What a surface sends back, with the shadowed sources kept apart: `CHANNEL_SHADOWED`'s and
/// `CHANNEL_LAMPED`'s records beside everything else.
struct SplitLight
{
    /// Everything but the shadowed sources.
    vec3 mRest;

    /// The sky's source and the lamps, each as though its rays got through, with its bit and its
    /// penumbra. `noShadowed` where nothing split them off.
    Shadowed mSky;
    Shadowed mLamps;
};

/// Which face of a surface a diffuse sample leaves by, and what the sample is then worth.
///
/// **A sheet has two hemispheres and one ray.** Its diffuse response is the near hemisphere whole and the
/// far one at `transmission`, so the far side is taken with probability `transmission / (1 + transmission)`
/// and either sample is weighed by `1 + transmission` — which is exactly the two integrals' sum, and one
/// ray however many faces there are. A solid never draws: its weight is one and its near face is its own.
///
/// **A sign rather than an axis, because a face is two vectors and not one.** The hemisphere is drawn about
/// the shading normal and bounded by the triangle's plane, and the far face is the far side of both — so
/// what the caller needs back is the turn to apply to each.
///
/// @param draw one number in `[0, 1)`, read only where there is a far side to choose.
float sampledFace(float transmission, float draw, out float weight)
{
    weight = 1.0 + transmission;
    return transmission > 0.0 && draw * weight > 1.0 ? -1.0 : 1.0;
}

/// Whether a direction drawn about the shading normal has gone behind the face it left by.
///
/// **The lobe is the shading normal's, which is what a shading normal is for, and the triangle is what
/// bounds it.** On this content a normal leans past its own plane often enough to matter — four hits in a
/// hundred by more than sixty degrees — and a direction drawn past it sets off *into* the surface.
/// Morrowind builds rooms out of sheets with no thickness, so nothing stops such a ray: it either leaves
/// the building, where a room hands nothing back and the point goes dark, or it lands on the far face of
/// the wall and hands back light the point cannot see.
///
/// **What a caller takes from such a direction is nothing, because nothing is the answer**: what it points
/// at is the inside of the surface.
///
/// @param plane the surface's `Surface::mGeometric` — or nothing at all for a point in a medium,
///        which has no triangle to be behind.
/// @param face which side `sampledFace` drew, which turns the plane along with the normal.
bool behindTheFace(vec3 towards, vec3 plane, float face)
{
    return dot(plane, plane) > 0.0 && face * dot(towards, plane) <= 0.0;
}

/// How much of the ambient a surface can see, as one cosine-weighted sample of its own hemisphere: the ray,
/// and what an answer to it is worth.
///
/// **The same integral the bounce already samples, one level further down.** A ray the eye found gathers a
/// real hemisphere and is occluded by whatever it hits; what *that* ray landed on was handed the whole
/// ambient whatever stood over it, so a point in a hollow was lit as though nothing stood over it. This is
/// the missing half, and it is a visibility ray rather than a bounce: nothing is shaded at the far end,
/// only asked whether there is one.
///
/// **One sample, and it is binary — `AMBIENT_EXTERIOR_RATE` of a sample out of doors.** That is as noisy as
/// a single sample can be, and it multiplies a term already carried by one, so it rides the same filter.
/// The estimator stays unbiased at any rate, which a cheaper guess would not be.
///
/// **A sheet asks both faces**, by `sampledFace`: a leaf in the open sees the sky over its back as well, at
/// `transmission` of what it sees over its front, so what reaches it runs to `1 + transmission` of a
/// solid's whole.
///
/// **A direction behind the surface's own triangle reaches nothing**, by `behindTheFace`: there is no
/// ambient inside a wall.
struct AmbientRay
{
    RayAsk mRay;

    /// Whether the answer is the ray's at all: nought, with no ray, behind the face or past the rate.
    bool mReaches;

    /// What the ray's answer is worth: the sheet's `sampledFace` weight, and over the rate out of
    /// doors.
    float mWeight;
    bool mRated;
};

/// @param normal the hemisphere's axis — a surface's shading normal, or nothing at all for a froxel
///        of the air, which is `weighLamps`' contract and means the same thing here: the air has no
///        side to face away from, so what stands over it is asked over the whole sphere rather than
///        over a hemisphere.
/// @param plane the surface's own triangle, as `behindTheFace` takes it and as the ray leaves it
///        (`leaveSurface`): nought for a froxel, which starts where it is.
/// @param rounding how far `position` can stand off that triangle, `Surface::mRounding`.
AmbientRay ambientRayOf(vec3 position, vec3 normal, vec3 plane, float rounding, float transmission, uint seed)
{
    uint state = randomSeed(seed);
    const vec2 draw = vec2(randomNext(state), randomNext(state));

    AmbientRay ray = AmbientRay(noRay(), false, 1.0, false);
    vec3 towards;

    if (dot(normal, normal) > 0.0)
    {
        const float face = sampledFace(transmission, transmission > 0.0 ? randomNext(state) : 0.0, ray.mWeight);
        towards = cosineDirection(normal * face, draw);

        if (behindTheFace(towards, plane, face))
            return ray;
    }
    else
        towards = sphereDirection(draw);

    // **How far to look for the occluder is what a room changes first.** In a room the ambient is the
    // `AMBI` fill, which stands for the bounces the room itself makes — so a wall is not an occluder of it,
    // it is the thing making it, and a ray run to the walls comes back blocked everywhere and takes the
    // light out of every interior. What does occlude it is what stands between a point and the room: the
    // pillow over the sheet, the chest against the wall, the underside of a table.
    if (!skyLights())
    {
        ray.mReaches = true;
        ray.mRay = ambientAsk(leaveSurface(position, plane * rounding, towards), towards, ROOM_FILL_REACH);
        return ray;
    }

    // Drawn last, so a solid's direction and a sheet's side are the numbers they were.
    if (randomNext(state) >= AMBIENT_EXTERIOR_RATE)
        return ray;

    ray.mReaches = true;
    ray.mRated = true;
    ray.mRay = ambientAsk(leaveSurface(position, plane * rounding, towards), towards, frame.mReach);
    return ray;
}

/// How much of the ambient the point an `AmbientRay` left can see, from what its ray answered.
float ambientAnswered(AmbientRay ray, RayAnswer answered)
{
    if (!ray.mReaches)
        return 0.0;

    const float through = throughAnswered(answered);
    return ray.mRated ? ray.mWeight * through / AMBIENT_EXTERIOR_RATE : ray.mWeight * through;
}

/// `ambientRayOf`, traced now.
float ambientReaching(vec3 position, vec3 normal, vec3 plane, float rounding, float transmission, uint seed)
{
    const AmbientRay ray = ambientRayOf(position, normal, plane, rounding, transmission, seed);
    return ambientAnswered(ray, answerAsked(ray.mRay));
}

/// What a see-through layer sends back, in the pieces the pane filter takes apart.
struct SeenPane
{
    /// What it glows with, which nothing drew: `litSurface` with no light arriving.
    vec3 mGlow;

    /// What a path end drew for it, whole — `GatherRun`'s diffuse half by the albedo, and `pathEnd` under
    /// its one occlusion ray by the ambient albedo — and what its lobe reflects of that, which the kernel
    /// composes unfiltered (`PaneStack::mDrawn`).
    vec3 mDrawn;
    vec3 mSpecular;

    SurfaceResponse mResponse;
};

/// What no layer sends back: what a walk that lights no layer hands `stepEye`.
SeenPane noPane()
{
    return SeenPane(vec3(0.0), vec3(0.0), vec3(0.0), noResponse());
}

/// The rays a path's end waits on: its occlusion ray, the sky's passage in its legs
/// (`SkyPassageRun`), and the held lamp's. Any of them none, which lets everything through, as
/// `passageAnswered` answers an untraced ray.
struct PathEndRays
{
    RayAsk mAmbient;
    RayAsk mSky;
    RayAsk mSkyOver;
    RayAsk mLamp;
};

/// What each of a path end's rays let through (`throughAnswered`): the sky's the product of its
/// legs', which is the joined passage's (`joinedPassage`), since an open half is one or nought.
struct PathEndSeen
{
    float mAmbient;
    float mSky;
    float mLamp;
};

/// A surface lit at the end of a path or in a stack the eye peels, before its rays are answered: the lights
/// a gather finds, and `pathEnd`, dimmed by one occlusion ray of its own, for the rest of the path.
///
/// **One statement of the tail the paths share**: the hit the eye's bounce found (`landing.comp`), and a
/// layer the eye looks through (`panes.comp`), the same terms with what was drawn kept apart from what was
/// not, since the pane filter averages the one over time and the glow is exact as it stands.
///
/// **The lights by the albedo and the fill by the ambient albedo**, as the rasterizer lights a fragment
/// `texture × (D × lit + A × ambient)`: `pathEnd` stands in for the rest of the path, which is the
/// ambient's place in the rasterizer's sum.
///
/// **Linear in what each ray lets through**, the gather being unsplit: so every term is worked out before
/// anything is traced (`pathEndSeenOf`), and only the terms and the rays stay live through the traversals,
/// where a walk of steps kept the surface, the gather's and the visit's records and the sky's passage
/// through each of them (`docs/rtx/architecture.md`, "A stage whose rays wait on no other's answer works
/// out its light before the first one").
///
/// **A pane is drawn at `AMBIENT_EXTERIOR_RATE`**, because the pane filter takes what it draws, as the
/// glossy filter takes what a lobe's path end draws at the same rate.
struct PathEndLight
{
    /// The fill per unit of what the occlusion ray let through: `pathEnd` by the ambient albedo.
    vec3 mFill;

    /// What the sky's source and the held lamp deliver per unit of what each one's ray let through:
    /// the diffuse half per unit albedo, net of the lobe's share, and the lobe whole.
    vec3 mSkyDiffuse;
    vec3 mSkySpecular;
    vec3 mLampDiffuse;
    vec3 mLampSpecular;

    PathEndRays mRays;
};

/// Stands the fill on `light` under an occlusion ray of its own.
///
/// @param seed the sequence the ray draws from, with the pixel's key: `SEED_AMBIENT_REACHING` says
///        why it is not the lamps'.
void addOccludedFill(inout PathEndLight light, Surface surface, uint seed)
{
    const AmbientRay ambientRay = ambientRayOf(
        surface.mPosition, surface.mNormal, surface.mGeometric, surface.mRounding, surface.mTransmission, seed);
    const float reaching = ambientRay.mReaches
        ? (ambientRay.mRated ? ambientRay.mWeight / AMBIENT_EXTERIOR_RATE : ambientRay.mWeight)
        : 0.0;
    light.mFill = occludedAlbedo(surface, surface.mAmbientAlbedo) * pathEnd(surface.mPosition, reaching);
    light.mRays.mAmbient = ambientRay.mRay;
}

/// The light at `surface`, a path's end, drawn for `pixel`, and no fill (`addOccludedFill`).
///
/// @param lamps the `SEED_` the lamp reservoir draws from with the pixel's key.
/// @param indirect the `SEED_` whether the gather runs is drawn from (`indirectGatherDrawn`).
/// @param path as `GatherRun` takes it.
PathEndLight pathEndLightOf(Surface surface, uvec2 pixel, uint lamps, uint indirect, uint path)
{
    const uint key = pixelKey(pixel);
    PathEndLight light;

    light.mFill = vec3(0.0);
    light.mRays.mAmbient = noRay();
    light.mSkyDiffuse = vec3(0.0);
    light.mSkySpecular = vec3(0.0);
    light.mLampDiffuse = vec3(0.0);
    light.mLampSpecular = vec3(0.0);
    light.mRays.mSky = noRay();
    light.mRays.mSkyOver = noRay();
    light.mRays.mLamp = noRay();

    // **The rate drawn before anything else and out of a sequence of its own**: the ordering in
    // `startGather` is what keeps a lamp arriving in the next cell from moving the penumbra of the one
    // already there, and a draw taken from that sequence would move every one of them.
    GatherRun gather = gatherRunOf(key, lamps, path, false, false);
    float rated = 1.0;
    if (path == PATH_INDIRECT && skyLights())
    {
        if (!indirectGatherDrawn(key, indirect))
            return light;

        rated = 1.0 / INDIRECT_LIGHT_RATE;
    }

    const Gloss gloss = glossOf(surface);
    SkyPassageRun sky = skyPassageRunOf(
        frame.mSun, vec3(0.0), vec3(0.0), BentPath(0.0, vec3(0.0), 0.0, vec2(0.0)), vec2(0.0), false);
    if (startGather(gather, sky, surface, gloss, pixel))
    {
        light.mRays.mSky = skyPassageFirstAsk(sky);
        light.mRays.mSkyOver = skyPassageOverAsk(sky);
        const SkyTerm estimate = gather.mSkyEstimate;
        light.mSkyDiffuse = (estimate.mDiffuse - estimate.mTaken) * rated;
        light.mSkySpecular = estimate.mSpecular * rated;
    }

    // **The clamped sum scaled by the held lamp's estimate of how much of it got through**, and the lobe's
    // share off the diffuse half: clamped on that one-lamp estimate instead, every pixel the held lamp
    // outshone the darkening kept light the mean did not have, and the ground near a negative lamp came out
    // brighter than its own lamps leave it.
    const Reservoir kept = gather.mKept;
    const float held = heldShare(kept);
    const vec3 lampArriving = kept.mRadiance * held
        * (1.0 - min(gather.mDarkening / max(kept.mUnshadowed, vec3(1e-30)), vec3(1.0)));
    const vec3 lampTaken = gloss.mGlossy ? lampArriving * kept.mFresnel : vec3(0.0);
    light.mLampDiffuse = (lampArriving - lampTaken) * rated;
    light.mLampSpecular = (gloss.mGlossy ? kept.mSpecular * held : vec3(0.0)) * rated;
    light.mRays.mLamp = gatherLampAsk(gather, surface);
    return light;
}

/// What `rays` let through, traced in a fixed order, a turn each, at one traversal site: none of
/// them waits on another's answer. The turn is the same in every lane, so which ray a turn traces is
/// a branch the whole warp takes, and a lane with no such ray waits out that one traversal.
PathEndSeen pathEndSeenOf(PathEndRays rays)
{
    PathEndSeen seen = PathEndSeen(1.0, 1.0, 1.0);
    [[dont_unroll]] for (uint turn = 0u; turn < 4u; ++turn)
    {
        RayAsk ask = rays.mLamp;
        if (turn == 0u)
            ask = rays.mAmbient;
        else if (turn == 1u)
            ask = rays.mSky;
        else if (turn == 2u)
            ask = rays.mSkyOver;

        const float through = throughAnswered(answerAsked(ask));
        if (turn == 0u)
            seen.mAmbient = through;
        else if (turn == 3u)
            seen.mLamp = through;
        else
            seen.mSky *= through;
    }
    return seen;
}

/// The layer `surface` the eye peels at `pixel`, its light before its rays are answered: drawn as a
/// path's end with a lamp sequence of its own (`paneAmbientSeed`, `paneSeed`).
PathEndLight paneLightOf(Surface surface, uvec2 pixel, uint layer)
{
    PathEndLight light = pathEndLightOf(surface, pixel, paneSeed(layer), SEED_INDIRECT_LIGHT, PATH_SEEN);
    addOccludedFill(light, surface, pixelKey(pixel) + paneAmbientSeed(layer));
    return light;
}

/// The layer `light` lit, once its rays let `seen` through.
SeenPane seenPaneOf(Surface surface, PathEndLight light, PathEndSeen seen)
{
    return SeenPane(litSurface(surface, vec3(0.0), vec3(0.0)),
        surface.mAlbedo * (light.mSkyDiffuse * seen.mSky + light.mLampDiffuse * seen.mLamp)
            + light.mFill * seen.mAmbient,
        light.mSkySpecular * seen.mSky + light.mLampSpecular * seen.mLamp, responseOf(surface, vec3(1.0)));
}

/// What one bounce brings back, in the two halves `storeBounced` writes apart.
struct Bounce
{
    /// Per unit albedo, as the direct light's diffuse half is: the composite multiplies it by the
    /// diffuse albedo.
    vec3 mDiffuse;

    /// The share of `mDiffuse` that is the fill, `Arriving::mFill`: the composite multiplies it by
    /// the ambient albedo in place of the diffuse one.
    vec3 mFill;

    /// Whole: what the lobe reflects toward the eye. **It stays out of the indirect channel, because
    /// it is not multiplied by the diffuse albedo** — a metal has none, and in the indirect term its
    /// whole reflection would be multiplied by nought.
    vec3 mSpecular;
};

/// Which way a bounce leaves, and what it is worth.
struct BounceDraw
{
    vec3 mTowards;

    /// What light arriving along `mTowards` is worth to the half that drew it, over the chance of
    /// every draw that chose it but the sheet's side. Nought where the lobe reflected below the
    /// shading normal's horizon.
    vec3 mWeight;

    /// Whether the lobe drew it. **A reflection is a picture of the world**, so its ray draws the
    /// faces the world shows, as the water's does, and its far hit is shaded as seen — `PATH_SEEN`
    /// says why.
    bool mSpecular;

    /// How fast the ray's cone opens: `BOUNCE_SPREAD` for the diffuse half, and for the lobe the pixel's
    /// own spread widened by the lobe's width, `ggxConeWidth`, as the water widens its reflection by its
    /// slopes.
    float mSpread;
};

/// What arrives along a bounce, whole, and the part of it the surface that drew it reflects by its ambient
/// albedo.
///
/// **The rest is reflected by the diffuse albedo**: the lights at the far hit and its glow, which is lamp
/// light bounced off another surface.
struct Arriving
{
    vec3 mWhole;

    /// The rasterizer's ambient term: the sky the ray escaped to, which is the fill out of doors, or
    /// the far hit's `pathEnd`, which is the fill a path ends at.
    vec3 mFill;
};

/// What a bounce brings back when it reaches nothing: the fill, whole.
///
/// **A room has nothing outside it, so a ray that got out of one brings back nothing.** The dome a room
/// draws is its fog colour standing in for the *picture* wherever a ray leaves the shell, and it is far
/// brighter than the room itself: lighting with it drew a bright band along the foot of every wall.
/// `mAmbientFromSky` is where a cell answers whether its sky is a light, and `ambientReaching` reads the
/// same field to decide how far to look for what blocks the fill.
///
/// **The lobe's escape finds the sky a mirror shows**, `reflectedSky`, because a reflection is a picture of
/// the world: the deck, the sheets and the stars where they are, and no fill.
vec3 bounceEscape(vec3 position, BounceDraw drawn, vec3 weight)
{
    if (!skyLights())
        return vec3(0.0);

    const vec3 sky = drawn.mSpecular ? reflectedSky(position, drawn.mTowards, 0.5 * drawn.mSpread, false)
                                     : skyGlow(drawn.mTowards);
    const vec3 daylight = daylightReaching(position);
    return weight * sky * daylight;
}

/// What arrives along a bounce that reached nothing, `escaped` (`bounceEscape`): the fill, whole.
Arriving escapedArriving(vec3 escaped)
{
    return Arriving(escaped, escaped);
}

/// Which way the eye's bounce leaves a surface, and what the light that arrives along it is worth.
///
/// **A Lambert surface draws what it always drew**: the cosine about the face `sampledFace` chose, worth
/// one.
///
/// **A glossy surface draws which half, in proportion to what each sends toward the eye**: the lobe at the
/// chance `lum(Es) / (lum(Es) + lum(c_diff))`, its specular albedo against its diffuse one, and each half's
/// sample divided by its own chance, which keeps the sum unbiased.
///
/// **Both directions are drawn and the chance selects one.** Which half is a coin per lane, so a branch on
/// it would run both halves in nearly every warp; the select runs each once and jumps nowhere.
///
/// **Off the near face only.** The lobe reflects, and the far face of a sheet transmits: `GatherRun` gives
/// a light behind a sheet no lobe and its diffuse half the whole of it, and so does this.
///
/// **One pair for either half**, `STREAM_BOUNCE`'s: only one half is kept, so the pair's spread across the
/// screen serves whichever it is.
///
/// @param cone the eye's cone at the stage that found the surface — the arms' camera for the
///        player's own arms — whose spread a reflection widens by.
/// @param ray which of the frame's bounce rays this is (`unitPairOfRay`), and `lobeSeed` the sequence
///        its choice of half is drawn from (`BounceSeeds::mLobe`).
BounceDraw bounceDraw(Surface surface, Gloss gloss, float face, uvec2 pixel, Cone cone, uint ray, uint lobeSeed)
{
    const vec2 draw = unitPairOfRay(pixel, STREAM_BOUNCE, ray);
    const vec3 scattered = cosineDirection(surface.mNormal * face, draw);

    BounceDraw drawn;
    drawn.mTowards = scattered;
    drawn.mWeight = vec3(1.0);
    drawn.mSpecular = false;
    drawn.mSpread = BOUNCE_SPREAD;

    if (!gloss.mGlossy || face < 0.0)
        return drawn;

    // A metal's diffuse albedo is nought and its chance one, which the draw always takes; the
    // diffuse weight it divides by nought is never the one selected.
    const float reflected = dot(gloss.mAlbedo, LUMINANCE_WEIGHTS);
    const float chance = reflected / (reflected + dot(surface.mAlbedo, LUMINANCE_WEIGHTS));

    uint lobe = randomSeed(pixelKey(pixel) + lobeSeed);
    const bool specular = randomNext(lobe) < chance;

    const LobeSample sampled = lobeSample(gloss, draw);
    const vec3 diffuseWeight = (1.0 - fresnelAt(gloss, normalize(gloss.mToEye + scattered))) / (1.0 - chance);
    const float lobeSpread
        = min(cone.mSpread + ggxConeWidth(gloss.mAlpha, BOUNCE_SPREAD), BOUNCE_SPREAD);

    drawn.mTowards = specular ? sampled.mTowards : scattered;
    drawn.mWeight = specular ? sampled.mWeight / chance : diffuseWeight;
    drawn.mSpecular = specular;
    drawn.mSpread = specular ? lobeSpread : BOUNCE_SPREAD;

    return drawn;
}

/// What a solid the eye found sends back, in the channels' pieces, but its bounce: `BounceRun`'s,
/// which `bounce.comp` traces.
struct SeenSolid
{
    /// Everything resolved but what a filter takes, the glow, in `mRest`, and what the sky's source
    /// and the lamps add and whether each one's ray got through: `CHANNEL_SHADOWED` and
    /// `CHANNEL_LAMPED`.
    SplitLight mLight;

    /// What the lobe reflects of the lamps, per unit of `SurfaceResponse::mSpecular`, and the perceptual
    /// roughness of the lobe that reflects it, or `SPECULAR_NO_LOBE`: `CHANNEL_SPECULAR`, to which the
    /// bounce adds its own.
    vec3 mSpecular;
    float mRoughness;
};

/// What a solid is in the filter's terms beside its light, and its lobe's roughness, out of the
/// surface and its gloss (`glossOf`) alone: what `solidLightOf` makes of it, and what the trace writes
/// of a solid whose light the direct stage gathers (`direct.comp`).
struct SolidLook
{
    SurfaceResponse mResponse;
    float mRoughness;
};

SolidLook solidLookOf(Surface surface, Gloss gloss)
{
    // **The two the composite multiplies the bounce and the fill back by, and so those alone**: the direct
    // stage lights the surface by `Surface::mAlbedo`. Past the filter, for the reason the albedo is: a crack
    // is detail the light behind it is not.
    SurfaceResponse response = responseOf(surface, specularModulation(gloss));
    response.mDiffuse = occludedAlbedo(surface, response.mDiffuse);
    response.mAmbient = occludedAlbedo(surface, response.mAmbient);
    return SolidLook(response, gloss.mGlossy ? surface.mRoughness : SPECULAR_NO_LOBE);
}

/// What a solid the eye found sends back before its shadow rays are answered, and the rays: what
/// `seenSolidOf` makes a `SeenSolid` of once they are (`direct.comp`).
///
/// **What reaches it from everything that is not a light is its bounce, `BounceRun`'s**, which stages of
/// their own trace and visit (`bounce.comp`, `landing.comp`): the one kernel carried every walk's record at
/// once, and the bounce's landing was a gather and a visit more.
///
/// **Everything but the rays' answers worked out first**, the gather's start and the solid's look, so only
/// these and the rays stay live through the traversals: a walk of steps kept the surface and the gather's
/// record through each of them, and the stage held 128 to 192 registers on RDNA 2.
///
/// **The gather is split** (`GatherRun::mSplit`), at the eye's own blue noise: the sky's source and the
/// lamps go to the shadow denoiser apart, and not to the wavelet with the bounce — direct light and
/// indirect apart, as NRD has them.
struct SolidLight
{
    /// The glow, `SplitLight::mRest`, and each shadowed source's light as though its ray got
    /// through, `Shadowed::mLight`.
    vec3 mRest;
    vec3 mSky;
    vec3 mLamps;

    /// What the lobe reflects of the held lamp before what its share and its ray let through, the
    /// share, and what the lobe's light is divided by (`specularModulation`): nought where the
    /// surface has no lobe. And the lobe's roughness, `SolidLook::mRoughness`.
    vec3 mLampSpecular;
    float mHeld;
    vec3 mModulation;
    float mRoughness;

    /// What each bit's penumbra is made of, as `seenSolidOf` makes it of its ray's occluder: the sky's
    /// source and how far a penumbra on this surface stretches toward it; the held lamp's radius, distance
    /// and stretch, and whether a lamp was held; whether each bit is drawn among its field's sources
    /// (`drawnAmong`); and the surface's footprint they are counted in.
    SkySource mSkySource;
    float mSkyStretch;
    bool mSkyDrawn;
    bool mLampHeld;
    float mLampRadius;
    float mLampDistance;
    float mLampStretch;
    bool mLampDrawn;
    float mFootprint;

    /// The two draws that decide whether each bit passes what its ray's translucent surfaces let
    /// through (`SEED_SHADOW_THROUGH`).
    vec2 mPassing;

    /// The sky's passage, `SkyPassageRun`'s legs and whether it is bent, which joins them
    /// (`joinedPassage`), and the held lamp's ray: any of them none, which `passageAnswered`
    /// answers open.
    RayAsk mSkyRay;
    RayAsk mSkyOverRay;
    bool mSkyBent;
    float mSkyPath;
    RayAsk mLampRay;
};

/// `surface`'s light before its rays, the solid the eye found at `pixel`.
SolidLight solidLightOf(Surface surface, uvec2 pixel)
{
    // A path the eye sees, which no rate draws (`GatherRun::mPath`).
    GatherRun gather = gatherRunOf(pixelKey(pixel), SEED_LAMPS_EYE, PATH_SEEN, true, true);
    const Gloss gloss = glossOf(surface);
    SkyPassageRun sky = skyPassageRunOf(
        frame.mSun, vec3(0.0), vec3(0.0), BentPath(0.0, vec3(0.0), 0.0, vec2(0.0)), vec2(0.0), false);
    const bool skyAsked = startGather(gather, sky, surface, gloss, pixel);

    SolidLight light;
    light.mSkyRay = skyAsked ? skyPassageFirstAsk(sky) : noRay();
    light.mSkyOverRay = skyAsked ? skyPassageOverAsk(sky) : noRay();
    light.mSkyBent = skyAsked && skyPassageBent(sky);
    light.mSkyPath = sky.mBent.mPath;
    light.mLampRay = gatherLampAsk(gather, surface);

    // **Split, every lamp's light as though its ray got through, and the held lamp's bit**: Heitz et al.
    // 2018's ratio estimator, the unshadowed light exact and only its visibility drawn.
    const Reservoir kept = gather.mKept;
    light.mRest = litSurface(surface, vec3(0.0), vec3(0.0));
    light.mSky = surface.mAlbedo * gather.mSkyDiffuse + gather.mSkySpecular;
    light.mLamps = surface.mAlbedo * max(kept.mUnshadowed - gather.mDarkening, vec3(0.0));

    light.mLampSpecular = gloss.mGlossy ? kept.mSpecular : vec3(0.0);
    light.mHeld = heldShare(kept);
    light.mModulation = specularModulation(gloss);
    light.mRoughness = solidLookOf(surface, gloss).mRoughness;

    light.mSkySource = gather.mSky;
    light.mSkyStretch = gather.mSkyStretch;
    light.mSkyDrawn = drawnAmong(gather.mSkyCarried, gather.mSkyWeight);
    // The held lamp's row, read only where one was held: a picture no lamp lights may have no row.
    light.mLampHeld = kept.mWeight > 0.0;
    light.mLampRadius = 0.0;
    light.mLampDistance = 0.0;
    if (light.mLampHeld)
    {
        const GpuLight lamp = lightAt(kept.mLamp);
        light.mLampRadius = lamp.mSourceRadius;
        light.mLampDistance = length(lamp.mPosition - kept.mFrom);
    }

    light.mLampStretch = gather.mLampStretch;
    light.mLampDrawn = drawnAmong(kept.mWeight, kept.mTotal);
    light.mFootprint = gather.mFootprint;

    uint passing = randomSeed(gather.mKey + SEED_SHADOW_THROUGH);
    light.mPassing.x = randomNext(passing);
    light.mPassing.y = randomNext(passing);

    // **The relief's own shadow on the sources' light** (`reliefLit`), whole and not drawn into a bit, so the
    // shadow filter blurs no shadow the size of a texel: the sky's source, and the held lamp's over every lamp,
    // as the ratio estimator weighs their light by its ray.
    if (HAS_RELIEF && surface.mReliefSite.mMap != NO_TEXTURE)
    {
        light.mSky *= reliefLit(surface.mReliefSite, gather.mSky.mDirection, gather.mSky.mLimb);
        if (light.mLampHeld)
        {
            const vec3 toLamp = lightAt(kept.mLamp).mPosition - surface.mPosition;
            const float lampRelief = reliefLit(surface.mReliefSite, normalize(toLamp),
                light.mLampRadius / max(length(toLamp), light.mLampRadius));
            light.mLamps *= lampRelief;
            light.mLampSpecular *= lampRelief;
        }
    }
    return light;
}

/// What a solid sends back once its sky's passage answered `sky` and its lamp's ray `lamp`.
///
/// **What the translucent surfaces let through is drawn into each bit**, open where its ray got through and
/// a draw falls under it: the bit's mean is then the open half times the through, and the shadow denoiser
/// filters a pane's shadow as it filters a penumbra.
///
/// **Each penumbra in the surface's own footprints**, which is what the denoiser's reach is counted in; and
/// the whole reach where the bit was drawn — where its own source carries less than all but the floor of
/// its field's light, since that bit is noise however hard its shadow.
SeenSolid seenSolidOf(SolidLight light, Passage sky, Passage lamp)
{
    const bool skyPassed = light.mPassing.x < sky.mThrough;
    const bool lampPassed = light.mPassing.y < lamp.mThrough;
    const float skyRadius = skyPenumbra(light.mSkySource, sky.mOccluder) * light.mSkyStretch;
    const float lampRadius = light.mLampHeld
        ? lampPenumbra(light.mLampRadius, light.mLampDistance, lamp.mOccluder) * light.mLampStretch
        : SHADOW_PENUMBRA_CLEAR;

    SeenSolid seen;
    seen.mLight = SplitLight(light.mRest,
        Shadowed(light.mSky, sky.mOpen * (skyPassed ? 1.0 : 0.0),
            penumbraIn(light.mSkyDrawn || sky.mThrough < 1.0, skyRadius, light.mFootprint)),
        Shadowed(light.mLamps, lamp.mOpen * (lampPassed ? 1.0 : 0.0),
            penumbraIn(light.mLampDrawn || lamp.mThrough < 1.0, lampRadius, light.mFootprint)));
    seen.mSpecular = light.mLampSpecular * (light.mHeld * (lamp.mOpen * lamp.mThrough)) / light.mModulation;
    seen.mRoughness = light.mRoughness;
    return seen;
}

/// What a solid's one bounce brings back, off the half `bounceDraw` chose, in the pieces the channels keep:
/// the bounce's stage's draw and ray (`bounce.comp`), off the surface the eye's ray or a shore's bed ray
/// found.
///
/// **One bounce, traced only from the hit the eye found**: a walk with no recursion cannot bounce a bounce,
/// and it should not — what the second hit gathers is `pathEnd`, the flat ambient that stands in for the
/// rest of the path.
///
/// **Two stages, the bounce and its landing** (`bounce.comp`, `landing.comp`): the draw and the ray here,
/// whose landing the landing's stage resolves and visits (`landingLightOf`).
struct BounceRun
{
    /// What the solid's lobe is divided by, as `specularModulation` packs it (`lobeModulationWord`), which
    /// the lobe's half of the bounce leaves the walk divided by, as the lamps' half is
    /// (`SeenSolid::mSpecular`).
    uint mModulationWord;

    /// The bounce: its draw, what light along it is worth, where it left from, its ray where one is
    /// traced, and the escape where the answer is known without one (`bounceEscape`).
    BounceDraw mDrawn;
    vec3 mWeight;
    vec3 mPosition;
    bool mBounceTraced;
    RayAsk mBounceRay;
    vec3 mEscaped;
};

BounceRun bounceRunOf()
{
    BounceRun run;
    // One where no solid stands, which the landing's stage divides nothing by.
    run.mModulationWord = packRgb9e5(vec3(1.0));
    run.mDrawn = BounceDraw(vec3(0.0, 0.0, 1.0), vec3(0.0), false, BOUNCE_SPREAD);
    run.mWeight = vec3(0.0);
    run.mPosition = vec3(0.0);
    run.mBounceTraced = false;
    run.mBounceRay = noRay();
    run.mEscaped = vec3(0.0);
    return run;
}

/// The bounce's draw off `surface`, and its ray where one is traced: the escape where the answer is known
/// without one.
///
/// @param cone the eye's cone at the stage that found the surface — the arms' camera for the
///        player's own arms — whose spread a reflection widens by.
/// @param ray which of the frame's `VisibilityConstants::mBounceRays` this is, counting the first as
///        nought. **Each is worth its share of the pixel's bounce**, one over their count, so the
///        rays a frame traces average into the one bounce's channels (`storeBounced`).
void startBounce(inout BounceRun run, Surface surface, Cone cone, uvec2 pixel, uint ray)
{
    const BounceSeeds seeds = bounceSeedsAt(ray);

    // **The specular half the solid's own gather made**, which is the surface's alone (`glossOf`),
    // so it is made again here where no gather runs.
    const Gloss gloss = glossOf(surface);
    run.mModulationWord = lobeModulationWord(gloss);

    // A sheet bounces off either face, and `SEED_SHEET_SIDE` says why the side is not drawn from the pair
    // the direction is.
    uint sideState = randomSeed(pixelKey(pixel) + seeds.mSide);
    float sided;
    const float face = sampledFace(surface.mTransmission, randomNext(sideState), sided);

    const BounceDraw drawn = bounceDraw(surface, gloss, face, pixel, cone, ray, seeds.mLobe);
    run.mDrawn = drawn;
    run.mPosition = surface.mPosition;

    // A reflection below the shading normal's horizon brings nothing back, and is not traced to
    // find that out.
    if (behindTheFace(drawn.mTowards, surface.mGeometric, face) || !(brightest(drawn.mWeight) > 0.0))
        return;

    const vec3 weight = drawn.mWeight * sided * (1.0 / float(frame.mBounceRays));

    // **Far ground out of doors is handed the escape rather than asked whether it escaped**, which
    // is the same answer the miss below arrives at by tracing for it. `BOUNCE_REACH` says what that
    // costs and why the room is not in it.
    const vec3 fromEye = surface.mPosition - frame.mOrigin;
    if (skyLights() && surface.mGround && dot(fromEye, fromEye) > BOUNCE_REACH * BOUNCE_REACH)
    {
        run.mEscaped = bounceEscape(surface.mPosition, drawn, weight);
        return;
    }

    run.mWeight = weight;

    // **A ray query, and not a `traceRayEXT` the hardware could sort.** The one ray Shader Execution
    // Reordering's sources point at is this one, and sorting for it was measured: 20 percent slower out of
    // doors and 30 in a room, because a bounce indoors is short and lands on the same few surfaces, so
    // there is no coherence left to recover. A diffuse bounce is not drawn: it carries light, and a surface
    // it met from behind still carries it.
    run.mBounceTraced = true;
    run.mBounceRay
        = traceAsk(WorldRay(leaveSurface(surface.mPosition, stepOf(surface), drawn.mTowards), drawn.mTowards), 0.0,
            Cone(surface.mFootprint, drawn.mSpread), solidMask(frame.mRayMask), drawn.mSpecular);
}

/// The escape of a traced bounce that met nothing (`bounceEscape`).
vec3 bounceMissed(BounceRun run)
{
    return bounceEscape(run.mPosition, run.mDrawn, run.mWeight);
}

/// What a bounce's landing sends back along it, by the bounce's `weight`, before any of its rays is
/// answered (`PathEndLight`): what arrives whatever they find, what each one's answer multiplies, whole,
/// and the rays.
///
/// **A room's fill whole, with no occlusion ray of its own**: the ray a landing would answer stands a wall
/// further from the eye than anything it could show, and dropping it saved 0.1 to 0.3 ms a frame, took
/// black pixels off the rooms the fill lights, and left the noise and the fireflies no worse
/// (`docs/rtx/research/2026-11-05-room-ambient.md`).
///
/// **Its glow is counted here, because this is the only path it takes.** Nothing gives a glowing surface a
/// lamp of its own — `EMISSIVE_INTENSITY` says what measuring that showed — so a ray that lands on a
/// mushroom cap is what carries the cap's glow back to whatever sent it.
///
/// **A lamp's own model glows to nothing**, every point a bounce leaves having evaluated the lamps
/// (`INSTANCE_LAMP_BODY`).
///
/// **One visit with the path chosen, and not one per half.** A warp whose lanes drew both halves would run
/// them one after the other. Chosen at run time, the diffuse half's hit asks the moons and finds they weigh
/// nought.
struct LandingLight
{
    /// What arrives whatever the rays find: the glow.
    vec3 mGlow;

    /// What arrives per unit of what each ray let through (`PathEndSeen`): the fill, whole and as
    /// the fill alike, and the sky's source and the held lamp, whole.
    vec3 mFill;
    vec3 mSky;
    vec3 mLamp;

    PathEndRays mRays;
};

/// @param landing the bounce's landing (`storedLandingOf`).
/// @param lobe whether the lobe drew the bounce (`BounceDraw::mSpecular`).
/// @param weight what light arriving along the bounce is worth (`StoredBounce::mWeight`).
/// @param seeds the sequences the landing draws from, its ray's (`bounceSeedsAt`).
LandingLight landingLightOf(Surface landing, bool lobe, uvec2 pixel, vec3 weight, BounceSeeds seeds)
{
    const bool lampBody = (instanceAt(landing.mInstance).mClass & INSTANCE_LAMP_BODY) != 0u;
    const float keep = lampBody ? 0.0 : 1.0;
    landing.mEmissiveColour *= keep;
    landing.mEmitted *= keep;

    PathEndLight end
        = pathEndLightOf(landing, pixel, seeds.mLamps, seeds.mIndirect, lobe ? PATH_SEEN : PATH_INDIRECT);
    if (skyLights())
        addOccludedFill(end, landing, pixelKey(pixel) + seeds.mAmbient);
    else
        end.mFill = occludedAlbedo(landing, landing.mAmbientAlbedo) * pathEnd(landing.mPosition, 1.0);

    LandingLight light;
    light.mGlow = weight * litSurface(landing, vec3(0.0), vec3(0.0));
    light.mFill = weight * end.mFill;
    light.mSky = weight * (landing.mAlbedo * end.mSkyDiffuse + end.mSkySpecular);
    light.mLamp = weight * (landing.mAlbedo * end.mLampDiffuse + end.mLampSpecular);
    light.mRays = end.mRays;
    return light;
}

/// What arrived, in the halves the channels keep. **The lobe takes the whole of it**: a reflection
/// is a picture of the world, and nothing in the rasterizer's sum reflects one by the ambient colour.
Bounce bounceOf(Arriving arriving, bool lobe)
{
    return lobe ? Bounce(vec3(0.0), vec3(0.0), arriving.mWhole) : Bounce(arriving.mWhole, arriving.mFill, vec3(0.0));
}

#endif
