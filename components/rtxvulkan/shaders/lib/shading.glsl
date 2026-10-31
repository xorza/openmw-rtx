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
/// **Not a count of bounces.** What separates them is whether the result is looked at: a seabed
/// through water and a face in a mirror are each a bounce out and each is what the pixel shows, so
/// they are lit like anything else the eye can see — and so is what a glossy surface's lobe
/// reflects, which is the whole of what a metal shows. Only the diffuse hemisphere's far hit is a
/// term that nothing resolves on its own, and only there is a light worth dropping.
const uint PATH_SEEN = 0u;
const uint PATH_INDIRECT = 1u;

/// The direct light a surface sends back toward the eye, in its two halves.
struct DirectLight
{
    /// What reaches the diffuse half, per unit albedo, with the share the lobe reflected at each
    /// light taken off it.
    vec3 mDiffuse;

    /// What the lobe reflects toward the eye, whole: a specular half is not multiplied by the
    /// diffuse albedo.
    vec3 mSpecular;

    /// The sky's source, kept out of the two above where `GatherRun` was asked to split it: its diffuse
    /// half per unit albedo and net of the lobe's share, and its lobe whole, as though its ray got
    /// through, translucent surfaces and all — and whether the ray did, one or nought, drawn open by
    /// what the translucent surfaces it crossed let through. Nought and one where nothing was split.
    vec3 mSkyDiffuse;
    vec3 mSkySpecular;
    float mSkyOpen;

    /// The penumbra where the sky's ray was stopped, in the surface's own footprints:
    /// `SHADOW_PENUMBRA_CLEAR` where it got through or nothing was split, and
    /// `SHADOW_PENUMBRA_DRAWN` where the source it went to was drawn, or what it let through:
    /// `CHANNEL_SHADOWED`'s.
    float mSkyPenumbra;

    /// The lamps' the same: every lamp's diffuse half summed, per unit albedo and net of the lobe's
    /// share, and the held lamp's bit and penumbra (`CHANNEL_LAMPED`). Their
    /// lobe is in `mSpecular`, under the held lamp's own ray.
    vec3 mLampDiffuse;
    float mLampOpen;
    float mLampPenumbra;
};

/// A penumbra in the surface's own footprints, `footprint` world units each, from its radius in
/// world units: `SHADOW_PENUMBRA_DRAWN` where the bit was drawn, and `SHADOW_PENUMBRA_CLEAR` where no
/// ray was stopped.
float penumbraIn(bool drawn, float radius, float footprint)
{
    return drawn ? SHADOW_PENUMBRA_DRAWN
        : radius < SHADOW_PENUMBRA_CLEAR ? min(radius / max(footprint, 1e-6), SHADOW_PENUMBRA_CLEAR)
                                         : SHADOW_PENUMBRA_CLEAR;
}

/// Whether a shadow bit whose source carries `carried` of its field's `total` is drawn among sources:
/// one that carries less than all but `mShadowFloor` of the field's light is noise however hard its
/// shadow, and `penumbraIn` gives it the whole reach.
///
/// **Asked before its ray as well as after**, because a drawn bit's penumbra is the same whatever
/// stopped the ray: its ray needs no nearest occluder, and stops at the first (`passageAsk`). At dawn,
/// with the moons up beside the sun, every sky bit is drawn.
bool drawnAmong(float carried, float total)
{
    return carried < (1.0 - frame.mShadowFloor) * total;
}

/// What a penumbra measured square to its light is on a receiver the light meets at `cosine`: `1 /
/// cosine` longer along the light's azimuth, held to `SHADOW_PENUMBRA_STRETCH`. Square to the light, a
/// grazing penumbra at Morrowind's long dawn read three to six times narrower than it lay, under a
/// pixel, and took the shadow denoiser's hard path with its raw bits.
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

/// The *direct* light arriving at a point and turning back out of it: per unit albedo for the
/// diffuse half, and whole for the specular.
///
/// **Sources that can be asked where they are, and nothing else.** The sun and the lamps are each a
/// known direction and a shadow ray; everything that arrives by having bounced off something is
/// the bounce's (`BounceRun`), and adding a fill here as well would count that half twice.
///
/// One shadow ray per light that could reach at all, and none for a light the surface faces away
/// from — the two tests before it are what keep a cell's worth of lamps affordable.
///
/// What it reads of the surface: where it is, its shading normal, which side a light has to stand
/// on — `facingOf` picks between the plane and the interpolated normal, and `litCosine` says
/// why neither answers for both — how wide the cone that found it had grown, which is the scale the
/// caustics resolve waves at, and what a light on its far side is worth, `Surface::mTransmission`.
///
/// **The lobe is taken at the light the diffuse half drew, toward that light's centre**, and its
/// estimate divides by the same chances the diffuse one does. The centre is where the diffuse half's
/// cosine and the reservoir's weight are taken, and a lobe taken anywhere else is divided by a
/// weight that does not describe it: taken where the shadow ray went across a lamp's sphere, a lamp
/// whose centre stands at the horizon weighs next to nothing while its sampled point may stand well
/// above it, and the quotient has no bound. The draws are the diffuse half's and there are no
/// others, so a surface with no specular half draws, weighs and traces exactly what it did before
/// it could have one.
///
/// **A walk of three steps** for the loop that answers its rays (`direct.comp`, `bounce.comp`,
/// `panes.comp`): everything but the rays when it starts, from the walk's surface; the sky's ray,
/// `SkyPassageRun`'s; and the lamp's. The draws come from sequences of their own, so working the
/// lamps out before the sky's ray is answered moves none of them.
struct GatherRun
{
    /// `RUN_IDLE` once `mLit` holds the answer.
    uint mStep;

    /// Whether the surface has a specular half (`Gloss::mGlossy`), the one part of `glossOf` the
    /// lamp's step reads. **The flag and not the record**: the record is twenty-odd words the stage
    /// would hold through both shadow traversals, and whoever needs it after makes it again from the
    /// surface, as `solidLightOf` and `startBounce` do.
    bool mGlossy;

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

    /// Whether the sky's source and the lamps' diffuse half are handed back apart,
    /// `DirectLight::mSkyDiffuse` and `mLampDiffuse` and what is beside each, for the shadow
    /// denoiser (`CHANNEL_SHADOWED`, `CHANNEL_LAMPED`): the solid a pixel shows splits — the eye's
    /// own, or what a water path found — and the pane and the bounce compose. Only with
    /// `PATH_SEEN`: the split terms do not carry the rate that `PATH_INDIRECT` draws at.
    bool mSplit;

    /// Whether the shadow rays' draws come from the tile at the pixel (`STREAM_SUN_DISC`): the eye's
    /// own split hit's.
    bool mBlue;

    /// What the gather has found so far, and the sums it is made of: `mLit`'s own halves are
    /// written last.
    DirectLight mLit;
    vec3 mRadiance;
    vec3 mSpecular;
    vec3 mTaken;
    float mRated;

    /// The sky's sources as the start weighed them: unsplit, their estimate under the one ray;
    /// split, the drawn source and how far a penumbra on this surface stretches toward it.
    SkyTerm mSkyEstimate;
    SkySource mSky;
    float mSkyStretch;
    float mSkyCarried;
    float mSkyWeight;
    float mSkyThrough;

    /// The lamp the reservoir held, the draw its ray takes on the source (`gatherLampAsk`), what the
    /// darkening lamps take off, and how far a penumbra on this surface stretches toward the held
    /// lamp. **The draw and not the ray**: a whole `RayAsk` carried through the sky's traversal is
    /// registers the stage holds through its loop, and the ray is made again from the draw.
    Reservoir mKept;
    vec2 mLampDraw;
    vec3 mDarkening;
    float mLampStretch;

    float mFootprint;
};

const uint GATHER_START = 1u;
const uint GATHER_SKY = 2u;
const uint GATHER_LAMP = 3u;

GatherRun gatherRunOf(uint key, uint lamps, uint path, bool split, bool blue)
{
    GatherRun run;
    run.mStep = GATHER_START;
    run.mGlossy = false;
    run.mKey = key;
    run.mLamps = lamps;
    run.mPath = path;
    run.mSplit = split;
    run.mBlue = blue;
    run.mLit = DirectLight(vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), 1.0, SHADOW_PENUMBRA_CLEAR, vec3(0.0), 1.0,
        SHADOW_PENUMBRA_CLEAR);
    run.mRadiance = vec3(0.0);
    run.mSpecular = vec3(0.0);
    run.mTaken = vec3(0.0);
    run.mRated = 1.0;
    run.mSkyEstimate = SkyTerm(vec3(0.0), vec3(0.0), vec3(0.0));
    run.mSky = frame.mSun;
    run.mSkyStretch = 1.0;
    run.mSkyCarried = 0.0;
    run.mSkyWeight = 0.0;
    run.mSkyThrough = 1.0;
    run.mKept = noLamps();
    run.mLampDraw = vec2(0.0);
    run.mDarkening = vec3(0.0);
    run.mLampStretch = 1.0;
    run.mFootprint = 0.0;
    return run;
}

/// What the start of a gather works out of `surface` and asks of its sky: everything but the two
/// rays' answers.
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

    // What the lobe reflects, and the share of the diffuse half's light it took. Each stays nought
    // on a surface with no specular half, and taking nought off the diffuse half is exact.
    run.mRadiance = vec3(0.0);
    run.mSpecular = vec3(0.0);
    run.mTaken = vec3(0.0);

    run.mLit = DirectLight(vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), 1.0, SHADOW_PENUMBRA_CLEAR, vec3(0.0), 1.0,
        SHADOW_PENUMBRA_CLEAR);

    uint state = randomSeed(run.mKey + run.mLamps);

    // **Where on a source a shadow ray leaves from is drawn before anything is weighed**, so that it
    // does not depend on how many lamps the cell happened to hold: the two pairs sit at a fixed
    // place in the sequence and the reservoir's own draws follow them. Otherwise a lamp arriving in
    // the next cell along would move the penumbra of the one already there.
    // A pair aims the sky's one ray and a draw picks which of its sources the ray goes to.
    //
    // **The eye's own split hit takes the same three from the tile** (`STREAM_SUN_DISC`), and the
    // sequence steps past the hashed ones all the same, so the reservoir's draws stay where they
    // were.
    const vec2 sunHashed = vec2(randomNext(state), randomNext(state));
    const float skyHashed = randomNext(state);
    const vec2 lampHashed = vec2(randomNext(state), randomNext(state));
    const vec2 sunDraw = run.mBlue ? unitPair(pixel, STREAM_SUN_DISC) : sunHashed;
    const float skyPick = run.mBlue ? randomAt(pixel, STREAM_SKY_PICK) : skyHashed;
    const vec2 lampDraw = run.mBlue ? unitPair(pixel, STREAM_LAMP_DISC) : lampHashed;

    // **The sky's sources are weighed and drawn the way the lamps are.** What each would deliver
    // unshadowed is its weight — its cosine and its irradiance, which is everything about it that can
    // be known without tracing — one is drawn in proportion, one ray goes to it, and its share is
    // divided by the draw. In daylight the moons weigh nothing and the sun is always the draw; at
    // night the sun weighs nothing and the draw is between the moons; only the hour either side of
    // dusk spends one ray on two sources, and the accumulator carries the noise that buys.
    //
    // The cosine is taken against the source's direction *in air*, which is exact for the flat bed
    // this mostly lights: refraction at a level surface moves no flux across a horizontal patch, so
    // the irradiance on one below is the irradiance above times whatever the path took. A tilted
    // underwater surface would want the refracted direction and gets this one.
    //
    // **The disc is sampled for visibility and not for radiometry**, and that is the sharper of the
    // two estimators rather than a saving. Across the two degrees of the sun's shadow cone the cosine
    // varies by parts in a thousand, so drawing it as well would put variance into a term that has
    // none and leave the penumbra — the only part of the integral the cone is wide enough to matter
    // to — no better resolved for it. Masser subtends thirty-nine times the sun's angle, so the cone
    // it is drawn from is that much wider and the penumbra under everything it lights that much
    // softer.
    //
    // **The moons are asked only where the eye can see the surface.** A bounce's far hit is an
    // indirect term nothing resolves on its own, so a moon reaching it through a shadow ray of its
    // own was the dimmest half of the dimmest thing in the frame.
    //
    // **The pick is `pickByWeight`'s**, which says why it is made against the weights. It draws with
    // `skyPick` and the ray aims with the sun's pair, so every lamp draw below keeps its place. **The
    // three are named and not indexed**, for the reason `SkyChoice` gives: the pick is a value the
    // compiler cannot fold, and a local array read at one is a spill.
    const bool lunar = HAS_MOONS && run.mPath == PATH_SEEN;
    const SkyChoice sun = skyChoiceAt(SKY_SOURCE_SUN, facing, sunUp(), gloss);
    const SkyChoice masser = skyChoiceAt(SKY_SOURCE_MASSER, facing, lunar, gloss);
    const SkyChoice secunda = skyChoiceAt(SKY_SOURCE_SECUNDA, facing, lunar, gloss);

    // **A source under `mShadowFloor` of the sky's weight is never the one drawn** (decision 3): its
    // light is in the sum below, and it takes the drawn source's ray. Drawn, a daylight moon at a
    // thousandth of the sun made every sunlit bit a draw, and the shadow denoiser filtered every sun
    // shadow at its widest all day. What that costs is the minor source's own shadow, at most the
    // floor's share of the light.
    const float skyWeight = sun.mLight.mWeight + masser.mLight.mWeight + secunda.mLight.mWeight;
    const float minor = frame.mShadowFloor * skyWeight;
    const WeightedPick pick = pickByWeight(drawable(sun.mLight.mWeight, minor), drawable(masser.mLight.mWeight, minor),
        drawable(secunda.mLight.mWeight, minor), skyPick);

    // What the bit's source carries of the sky's light, as the pair `skyCarried / skyWeight`: its
    // weight over every source's, the minor ones counted against it. And what the translucent
    // surfaces its ray crossed let through, which a split bit is drawn by.
    run.mSkyCarried = 0.0;
    run.mSkyWeight = skyWeight;
    run.mSkyThrough = 1.0;
    const bool skyAsked = pick.mTotal > 0.0;
    if (skyAsked)
    {
        const SkyChoice picked = pick.mIndex == 0u ? sun : (pick.mIndex == 1u ? masser : secunda);
        run.mSkyCarried = picked.mLight.mWeight;

        // Split, the rays' own bit is handed back and the rest of the estimate is made as though
        // they got through: the product of the two is the estimate unsplit, and the bit is what a
        // denoiser filters in its place.
        // The shadow rays leave from where every ray off the surface leaves, and the light arrives
        // where the surface is: one refraction, a bent path to each.
        const SunUnderWater bent = sunUnderWater(picked.mSky.mDirection);
        skyPassage = skyPassageRunOf(picked.mSky, leaving, stepOf(surface), bentPathAt(leaving, bent), sunDraw,
            split && !drawnAmong(picked.mLight.mWeight, skyWeight));

        // **Each source's term whole, as the lamps' unshadowed sum is**: split, every source
        // with a weight is in the light the bit multiplies, so a pixel's hue is the sources' and
        // not the pick's; unsplit, the drawn one over its chance and the minor ones whole, under
        // the one ray.
        const SkyTerm sunTerm = skyTermOf(sun, position, surface.mFootprint, gloss);
        const SkyTerm masserTerm = skyTermOf(masser, position, surface.mFootprint, gloss);
        const SkyTerm secundaTerm = skyTermOf(secunda, position, surface.mFootprint, gloss);
        const SkyTerm pickedTerm = pick.mIndex == 0u ? sunTerm : (pick.mIndex == 1u ? masserTerm : secundaTerm);

        if (split)
        {
            const SkyTerm every = joinedTerms(joinedTerms(sunTerm, masserTerm, 1.0), secundaTerm, 1.0);
            run.mLit.mSkyDiffuse = every.mDiffuse - every.mTaken;
            run.mLit.mSkySpecular = every.mSpecular;
            run.mSky = picked.mSky;
            run.mSkyStretch = receiverStretch(picked.mCosine);
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

    // A lamp loses nothing to the water, where the sun and the sky both lose the column above the
    // point: it is usually standing in the same water as what it lights, and the depth over the two
    // of them is not between them.
    // **Every lamp is weighed and one is kept, so the cost is one shadow ray however many there
    // are.** Walking them all spends a ray apiece, which is a per-pixel cost against a per-cell
    // question: a room with a dozen candles was a dozen shadow rays for every pixel of it.
    //
    // Resampled importance sampling. A candidate's weight is what it would deliver *unshadowed*,
    // which is everything about a lamp that can be known without tracing — its reach, its falloff
    // and the cosine — so the one that survives is nearly always the one that mattered. The
    // estimator then divides by the chance it was kept, which is what makes this unbiased rather
    // than merely cheap: the sum of every weight, over the weight of the one held.
    //
    // **With one lamp in the cell it is exactly the arithmetic that was here before**: the sum is
    // that lamp's weight, the ratio is one, and what is left is the term that was always there.
    //
    // **The lobe takes the held lamp's own lobe, kept from the weighing**, and the reservoir's share:
    // the estimate of the lobe over every lamp is then the one the diffuse half makes, with the lobe
    // where the cosine was. A glossy surface weighs each lamp by both — `surfaceCandidate` says why —
    // and either estimate is unbiased under any weight positive where its term is.
    Reservoir kept = noLamps();
    // **Every lamp at a split hit, whose unshadowed sum must be exact, and a fixed count of
    // candidates everywhere else** (`VisibilityConstants::mLampCandidates`): a path's far end and a
    // pane cost what a lamp-dense cell costs only where it is seen. What is seen splits, the water's
    // legs among it (`VisitRun`), so a water pixel walks the cell once a leg.
    weighLamps(kept, state, position, facing, INV_PI, gloss, surface.mLampLit, split ? 0u : frame.mLampCandidates);
    kept.mFrom = leaving;
    run.mKept = kept;
    run.mLampDraw = lampDraw;

    // **The lamps that take light away take it off the lamps' exact sum and no further**, floored at
    // nought as the rasterizer clamps its lighting: the sun, the sky and the bounce stay whole.
    // Walked only where a lamp was held. With none, every lamp weighed nought, so its light is
    // nought or the albedo that would show it is (`surfaceCandidate`): unsplit the sum is nought
    // whatever is taken off it, and split it reaches the picture only times that albedo.
    run.mDarkening = darkeningAt(position, facing, INV_PI, surface.mLampLit && kept.mWeight > 0.0);
    run.mLampStretch = split && kept.mWeight > 0.0
        ? receiverStretch(litCosine(facing, normalize(lightAt(kept.mLamp).mPosition - position)))
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

/// Whether a gather a path's far end makes, keyed `key`, is drawn to run where the sky lights
/// (`INDIRECT_LIGHT_RATE`): asked by the gather and by the bounce that lists its landing
/// (`appendLandings`).
bool indirectGatherDrawn(uint key)
{
    uint rate = randomSeed(key + SEED_INDIRECT_LIGHT);
    return randomNext(rate) < INDIRECT_LIGHT_RATE;
}

/// @param surface the walk's surface, which the start reads, and the lamp's ray after it.
void stepGather(inout GatherRun run, inout SkyPassageRun skyPassage, Surface surface, uvec2 pixel, RayAnswer answered,
    inout RayAsk ask)
{
    if (run.mStep == GATHER_START)
    {
        const Gloss gloss = glossOf(surface);
        run.mGlossy = gloss.mGlossy;

        // **Drawn before anything else and out of a sequence of its own**: the ordering below is
        // what keeps a lamp arriving in the next cell from moving the penumbra of the one already
        // there, and a draw taken from that sequence would move every one of them.
        run.mRated = 1.0;
        if (run.mPath == PATH_INDIRECT && skyLights())
        {
            if (!indirectGatherDrawn(run.mKey))
            {
                run.mLit = DirectLight(vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), 1.0, SHADOW_PENUMBRA_CLEAR,
                    vec3(0.0), 1.0, SHADOW_PENUMBRA_CLEAR);
                run.mStep = RUN_IDLE;
                return;
            }

            run.mRated = 1.0 / INDIRECT_LIGHT_RATE;
        }

        if (startGather(run, skyPassage, surface, gloss, pixel))
        {
            run.mStep = GATHER_SKY;
            return;
        }

        ask = gatherLampAsk(run, surface);
        run.mStep = GATHER_LAMP;
        return;
    }

    if (run.mStep == GATHER_SKY)
    {
        const Passage passage = skyPassage.mPassage;
        if (run.mSplit)
        {
            run.mLit.mSkyOpen = passage.mOpen;
            run.mSkyThrough = passage.mThrough;
            run.mLit.mSkyPenumbra = skyPenumbra(run.mSky, passage.mOccluder) * run.mSkyStretch;
        }
        else
        {
            const float seen = passage.mOpen * passage.mThrough;
            run.mRadiance += run.mSkyEstimate.mDiffuse * seen;
            run.mSpecular += run.mSkyEstimate.mSpecular * seen;
            run.mTaken += run.mSkyEstimate.mTaken * seen;
        }

        ask = gatherLampAsk(run, surface);
        run.mStep = GATHER_LAMP;
        return;
    }

    const Reservoir kept = run.mKept;
    const bool split = run.mSplit;
    const Passage lampPass = passageAnswered(answered);
    const float lampSeen = lampPass.mOpen * lampPass.mThrough;
    const float held = heldShare(kept);

    // **Split, every lamp's light as though its ray got through, and the held lamp's bit**: Heitz
    // et al. 2018's ratio estimator, the unshadowed light exact and only its visibility drawn. The
    // bit is the held lamp's, which the reservoir drew in proportion to its weight. On a surface
    // with no lobe the weight is the light's luminance, so the sum times the bit is exact in
    // luminance on average; a lobe weighs a lamp by its reflection as well, so where the lamps'
    // rays disagree the bit leans toward the lamps the lobe reflects most. Exact wherever every ray
    // agrees. Correcting the sum by the held lamp's own estimate would lean nowhere, and put the
    // reservoir's speckle back into a light nothing filters.
    const vec3 lampsArriving = split ? kept.mUnshadowed : kept.mRadiance * held * lampSeen;

    // Split, the darkening comes off the sum the bit multiplies. Unsplit, the clamped sum is scaled
    // by the held lamp's estimate of how much of it got through: clamped on that one-lamp estimate
    // instead, every pixel the held lamp outshone the darkening kept light the mean did not have,
    // and the ground near a negative lamp came out brighter than its own lamps leave it.
    // `1 - min(d / u, 1)` is `max(u - d, 0) / u`, and exactly one where nothing darkens.
    const vec3 darkening = run.mDarkening;
    const vec3 lampDiffuse = split
        ? max(lampsArriving - darkening, vec3(0.0))
        : lampsArriving * (1.0 - min(darkening / max(kept.mUnshadowed, vec3(1e-30)), vec3(1.0)));

    if (run.mGlossy)
        run.mSpecular += kept.mSpecular * (held * lampSeen);

    if (split)
    {
        // **Each source's bit apart, for a field of its own each** (`CHANNEL_SHADOWED` says what one
        // bit for both cost). **What the translucent surfaces let through is drawn into each bit**,
        // open where its ray got through and a draw falls under it: the bit's mean is then the open
        // half times the through, the product the sum carried before, and the shadow denoiser
        // filters a pane's shadow as it filters a penumbra. In the sum, one ray's through was a
        // speckle under every pane and leaf that nothing filtered.
        uint passing = randomSeed(run.mKey + SEED_SHADOW_THROUGH);
        const bool skyPassed = randomNext(passing) < run.mSkyThrough;
        const bool lampPassed = randomNext(passing) < lampPass.mThrough;
        run.mLit.mSkyOpen *= skyPassed ? 1.0 : 0.0;
        run.mLit.mLampDiffuse = lampDiffuse;
        run.mLit.mLampOpen = lampPass.mOpen * (lampPassed ? 1.0 : 0.0);

        // In the surface's own footprints, which is what the denoiser's reach is counted in; and the
        // whole reach where the bit was drawn — where its own source carries less than all but the
        // floor of its field's light, since that bit is noise however hard its shadow. As products,
        // for the reason `pickByWeight` compares against weights and not quotients. A bit the
        // translucency was drawn into is noise as well.
        const bool skyDrawn = drawnAmong(run.mSkyCarried, run.mSkyWeight) || run.mSkyThrough < 1.0;
        const bool lampDrawn = drawnAmong(kept.mWeight, kept.mTotal) || lampPass.mThrough < 1.0;
        const float lampRadius = kept.mWeight > 0.0 ? lampPenumbra(kept, lampPass.mOccluder) * run.mLampStretch
                                                     : SHADOW_PENUMBRA_CLEAR;
        run.mLit.mSkyPenumbra = penumbraIn(skyDrawn, run.mLit.mSkyPenumbra, run.mFootprint);
        run.mLit.mLampPenumbra = penumbraIn(lampDrawn, lampRadius, run.mFootprint);
    }
    else
    {
        run.mRadiance += lampDiffuse;
        if (run.mGlossy)
            run.mTaken += lampDiffuse * kept.mFresnel;
    }

    run.mLit.mDiffuse = (run.mRadiance - run.mTaken) * run.mRated;
    run.mLit.mSpecular = run.mSpecular * run.mRated;
    run.mStep = RUN_IDLE;
}

/// What terminates a path: the cell's own ambient, dimmed by whatever stands over the point.
///
/// **A stand-in for every bounce that is not being traced**, which is what it always was — the
/// difference is that it is now one level down rather than added on top of the one that is. A
/// surface the eye can see gathers a real hemisphere; what *that* ray lands on gets this instead,
/// and the path stops there.
///
/// It stands in for light that arrived from above, so what it loses to water is the column straight
/// over the point — `daylightReaching`'s approximation, and the same one the bounce's own escape to
/// the sky uses.
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

/// What the lobe's light is divided by before the glossy filter averages it, and the composite
/// multiplies back: `gloss`'s split-sum specular albedo (`Gloss::mAlbedo`). Every channel a filter
/// averages holds light per unit of the albedo the composite puts back, so a history blended over a
/// replacer's speckled reflectance keeps the speckle sharp, as the bounce's demodulation keeps a
/// texture. Held at `SPECULAR_ALBEDO_FLOOR` from below, which says why it is
/// a floor, and one where there is no lobe.
///
/// **Rounded to what the answer packs before anything is divided by it** (`packRgb9e5`, whose
/// every value a half holds as well): the light divided by this and the channel the composite
/// multiplies by are one number, so the two meet to the rounding of a product. The floor survives
/// the rounding as a step of the shared exponent's mantissa, never nought, while every channel of
/// the albedo rounds below four.
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

/// What Night-Eye's lift adds to a surface, per unit of lift, in display values: its ambient albedo
/// encoded, as the rasterizer adds `texture × A × lift` to every fragment it lights — in the values
/// it displays, with nothing to occlude it and no exposure to adapt it away. `CHANNEL_LIFT`.
vec3 liftOf(Surface surface)
{
    return encodeSrgb(surface.mAmbientAlbedo);
}

/// What an ordinary lit surface sends back along the ray that found it. **One statement of what a
/// diffuse surface does with light, used at every depth** — writing it twice is how two would come
/// to disagree.
///
/// @param diffuse what reaches the diffuse half, per unit albedo: the lights `GatherRun` found and what
///        arrives from everything that is not a light — `pathEnd` at the hit a hemisphere found — or
///        whichever part of that the caller has not handed to a filter.
/// @param specular what the lobe reflects toward the eye, whole.
vec3 litSurface(Surface surface, vec3 diffuse, vec3 specular)
{
    // The emissive colour joins the light rather than the albedo, which is where the original engine
    // puts it: it sums the term with the diffuse and ambient light and multiplies the whole by the
    // texture (`files/shaders/compatibility/objects.frag:232`). Added past the albedo instead, a
    // mushroom cap carrying half against its stalk's nothing comes out flat white.
    //
    // The emissive *map* is the other way round, and that is the engine's doing too
    // (`objects.frag:244`): added after the multiply, so it glows through whatever the surface is
    // made of rather than being tinted by it.
    //
    // The lobe's light is added last, and whole.
    return surface.mAlbedo * (diffuse + surface.mEmissiveColour * EMISSIVE_INTENSITY) + surface.mEmitted + specular;
}

/// What a surface sends back, with the shadowed sources kept apart: `CHANNEL_SHADOWED`'s and
/// `CHANNEL_LAMPED`'s records beside everything else.
struct SplitLight
{
    /// Everything but the shadowed sources.
    vec3 mRest;

    /// The sky's source and the lamps, each as though its rays got through, with its bit and its
    /// penumbra (`DirectLight`). `noShadowed` where nothing split them off.
    Shadowed mSky;
    Shadowed mLamps;
};

/// The whole of a split light, with the shadowed sources as their rays found them.
vec3 composed(SplitLight light)
{
    return light.mRest + seenLight(light.mSky) + seenLight(light.mLamps);
}

/// The light of a surface of diffuse albedo `albedo` with what a gather split off kept apart, `rest`
/// beside it.
SplitLight splitLightOf(vec3 albedo, DirectLight lit, vec3 rest)
{
    return SplitLight(rest, Shadowed(albedo * lit.mSkyDiffuse + lit.mSkySpecular, lit.mSkyOpen, lit.mSkyPenumbra),
        Shadowed(albedo * lit.mLampDiffuse, lit.mLampOpen, lit.mLampPenumbra));
}

/// Which face of a surface a diffuse sample leaves by, and what the sample is then worth.
///
/// **A sheet has two hemispheres and one ray.** Its diffuse response is the near hemisphere whole
/// and the far one at `transmission`, so the far side is taken with probability
/// `transmission / (1 + transmission)` and either sample is weighed by `1 + transmission` — which
/// is exactly the two integrals' sum, and one ray however many faces there are. A solid never
/// draws: its weight is one and its near face is its own.
///
/// **A sign rather than an axis, because a face is two vectors and not one.** The hemisphere is
/// drawn about the shading normal and bounded by the triangle's plane, and the far face is the far
/// side of both — so what the caller needs back is the turn to apply to each.
///
/// @param draw one number in `[0, 1)`, read only where there is a far side to choose.
float sampledFace(float transmission, float draw, out float weight)
{
    weight = 1.0 + transmission;
    return transmission > 0.0 && draw * weight > 1.0 ? -1.0 : 1.0;
}

/// Whether a direction drawn about the shading normal has gone behind the face it left by.
///
/// **The lobe is the shading normal's, which is what a shading normal is for, and the triangle is
/// what bounds it.** On this content a normal leans past its own plane often enough to matter — four
/// hits in a hundred by more than sixty degrees — and a direction drawn past it sets off *into* the
/// surface. Morrowind builds rooms out of sheets with no thickness, so nothing stops such a ray:
/// it either leaves the building, where a room hands nothing back and the point goes dark, or it
/// lands on the far face of the wall and hands back light the point cannot see. Over a converged
/// frame of a room those rays are most of the mean escaped share and nearly all of the worst
/// pixels'.
///
/// **What a caller takes from such a direction is nothing, because nothing is the answer**: what it
/// points at is the inside of the surface. The cost is the part of the lobe that leans past the
/// plane, and that loss is the shading normal's own rather than one made here.
///
/// @param plane the surface's `Surface::mGeometric` — or nothing at all for a point in a medium,
///        which has no triangle to be behind.
/// @param face which side `sampledFace` drew, which turns the plane along with the normal.
bool behindTheFace(vec3 towards, vec3 plane, float face)
{
    return dot(plane, plane) > 0.0 && face * dot(towards, plane) <= 0.0;
}

/// How much of the ambient a surface can see, as one cosine-weighted sample of its own hemisphere:
/// the ray, and what an answer to it is worth.
///
/// **The same integral the bounce already samples, one level further down.** A ray the eye found
/// gathers a real hemisphere and is occluded by whatever it hits; what *that* ray landed on was
/// handed the whole ambient whatever stood over it, so a point in a hollow was lit as though nothing
/// stood over it. This is the missing half, and it is a visibility ray rather than a bounce: nothing
/// is shaded at the far end, only asked whether there is one.
///
/// **One sample, and it is binary — `AMBIENT_EXTERIOR_RATE` of a sample out of doors.** That is as
/// noisy as a single sample can be, and it multiplies a term already carried by one, so it rides the
/// same filter. The estimator stays unbiased at any rate, which a cheaper guess would not be.
///
/// **A sheet asks both faces**, by `sampledFace`: a leaf in the open sees the sky over its back as
/// well, at `transmission` of what it sees over its front, so what reaches it runs to
/// `1 + transmission` of a solid's whole. The side is drawn after the direction and only where
/// there is one to draw, so a solid's sequence is what it was.
///
/// **A direction behind the surface's own triangle reaches nothing**, by `behindTheFace`: there is
/// no ambient inside a wall.
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

    // **How far to look for the occluder is what a room changes first.** In a room the ambient is
    // the `AMBI` fill, which stands for the bounces the room itself makes — so a wall is not an
    // occluder of it, it is the thing making it, and a ray run to the walls comes back blocked
    // everywhere and takes the light out of every interior. What does occlude it is what stands
    // between a point and the room: the pillow over the sheet, the chest against the wall, the
    // underside of a table. A room keeps every sample too — `AMBIENT_EXTERIOR_RATE` says why.
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

/// What a surface a path ends at sends back, with the fill apart from the lights.
struct PathEnd
{
    /// `GatherRun`'s light through `litSurface`, the glow with it, and the shadowed sources apart where
    /// the caller split them off.
    SplitLight mLit;

    /// The fill's two factors: `pathEnd` under the surface's one occlusion ray, and the ambient
    /// albedo that reflects it.
    vec3 mAmbient;
    vec3 mAmbientAlbedo;
};

/// What a path end sends back by its ambient albedo.
vec3 fillOf(PathEnd end)
{
    return end.mAmbientAlbedo * end.mAmbient;
}

/// A path end's light with the fill joined to the rest.
SplitLight joinedLight(PathEnd end)
{
    return SplitLight(fillOf(end) + end.mLit.mRest, end.mLit.mSky, end.mLit.mLamps);
}

/// What a see-through layer sends back, in the pieces the pane filter takes apart.
struct SeenPane
{
    /// What it glows with, which nothing drew: `litSurface` with no light arriving.
    vec3 mGlow;

    /// What a path end drew for it, whole — `GatherRun`'s diffuse half by the albedo, and `pathEnd`
    /// under its one occlusion ray by the ambient albedo — and what its lobe reflects of that, which
    /// the kernel composes unfiltered (`PaneStack::mDrawn`). **Whole and not per unit albedo**,
    /// because the two halves take two albedos, and the pane filter averages a layer over time
    /// alone, so nothing needs the light apart from them.
    vec3 mDrawn;
    vec3 mSpecular;

    SurfaceResponse mResponse;
};

/// A surface lit at the end of a path or in a stack the eye peels: the lights a gather finds, and
/// `pathEnd`, dimmed by one occlusion ray of its own, for the rest of the path.
///
/// **One statement of the tail the paths share**: the far end of a water ray, and the hit the eye's
/// bounce found (`VISIT_PATH_END`); and a layer the eye looks through (`VISIT_PANE`), the same terms
/// with what was drawn kept apart from what was not, since the pane filter averages the one over
/// time and the glow is exact as it stands.
///
/// **The lights by the albedo and the fill by the ambient albedo**, as the rasterizer lights a
/// fragment `texture × (D × lit + A × ambient)`: `pathEnd` stands in for the rest of the path, which
/// is the ambient's place in the rasterizer's sum.
///
/// **The shadowed sources composed into the light**, since a filter takes every path end whole: the
/// wavelet the bounce's, and the pane filter a pane's.
///
/// **A pane is drawn at `AMBIENT_EXTERIOR_RATE`**, because the pane filter takes what it draws, as
/// the glossy filter takes what a lobe's path end draws at the same rate. **The same terms, so a
/// pane composed from these is the pane a path end would shade**: the glow plus the drawn light plus
/// the lobe is its light, to its rounding.
///
/// **A walk of three steps** (`bounce.comp`, `panes.comp`): the occlusion ray, then the gather,
/// then the answer, all of the walk's surface — which nothing between the start and the answer
/// resolves over.
struct VisitRun
{
    /// `RUN_IDLE` once `mEnd` or `mPane` holds the answer.
    uint mStep;

    uint mKind;

    /// The pixel's own key, `pixelKey`, and `mAmbient` and `mLamps` the `SEED_` the occlusion ray
    /// and the lamp reservoir draw from with it. Two, for the reason `SEED_AMBIENT_REACHING` gives.
    uint mKey;
    uint mAmbient;
    uint mLamps;

    /// As `GatherRun` takes it.
    uint mPath;

    AmbientRay mAmbientRay;
    float mReaching;

    PathEnd mEnd;
    SeenPane mPane;
};

const uint VISIT_PATH_END = 0u;
const uint VISIT_PANE = 1u;

const uint VISIT_START = 1u;
const uint VISIT_AMBIENT = 2u;
const uint VISIT_GATHER = 3u;

VisitRun visitRunOf(uint kind, uint key, uint ambient, uint lamps, uint path)
{
    VisitRun run;
    run.mStep = VISIT_START;
    run.mKind = kind;
    run.mKey = key;
    run.mAmbient = ambient;
    run.mLamps = lamps;
    run.mPath = path;
    run.mAmbientRay = AmbientRay(noRay(), false, 1.0, false);
    run.mReaching = 0.0;
    run.mEnd = PathEnd(SplitLight(vec3(0.0), noShadowed(), noShadowed()), vec3(0.0), vec3(0.0));
    run.mPane = SeenPane(vec3(0.0), vec3(0.0), vec3(0.0), noResponse());
    return run;
}

/// `VISIT_PATH_END`'s visit: a surface a path ends at.
VisitRun pathEndRunOf(uint key, uint ambient, uint lamps, uint path)
{
    return visitRunOf(VISIT_PATH_END, key, ambient, lamps, path);
}

/// `VISIT_PANE`'s visit: a layer the eye peels, drawn as a path's end with a lamp sequence of its
/// own.
VisitRun paneRunOf(uint key, uint ambient, uint lamps)
{
    return visitRunOf(VISIT_PANE, key, ambient, lamps, PATH_SEEN);
}

/// @param surface the walk's surface, the one visited.
void stepVisit(inout VisitRun run, inout GatherRun gather, Surface surface, RayAnswer answered, inout RayAsk ask)
{
    if (run.mStep == VISIT_START)
    {
        run.mAmbientRay = ambientRayOf(surface.mPosition, surface.mNormal, surface.mGeometric, surface.mRounding,
            surface.mTransmission, run.mKey + run.mAmbient);
        ask = run.mAmbientRay.mRay;
        run.mStep = VISIT_AMBIENT;
        return;
    }

    if (run.mStep == VISIT_AMBIENT)
    {
        run.mReaching = ambientAnswered(run.mAmbientRay, answered);
        gather = gatherRunOf(run.mKey, run.mLamps, run.mPath, false, false);
        run.mStep = VISIT_GATHER;
        return;
    }

    const DirectLight lit = gather.mLit;
    if (run.mKind == VISIT_PATH_END)
        run.mEnd = PathEnd(splitLightOf(surface.mAlbedo, lit, litSurface(surface, lit.mDiffuse, lit.mSpecular)),
            pathEnd(surface.mPosition, run.mReaching), surface.mAmbientAlbedo);
    else
        run.mPane = SeenPane(litSurface(surface, vec3(0.0), vec3(0.0)),
            surface.mAlbedo * lit.mDiffuse + surface.mAmbientAlbedo * pathEnd(surface.mPosition, run.mReaching),
            lit.mSpecular, responseOf(surface, vec3(1.0)));
    run.mStep = RUN_IDLE;
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

    /// How fast the ray's cone opens: `BOUNCE_SPREAD` for the diffuse half, and for the lobe the
    /// pixel's own spread widened by the lobe's width, `ggxConeWidth`, as the water widens its
    /// reflection by its slopes. Never past `BOUNCE_SPREAD`, which is what a lobe as wide as the
    /// diffuse one reads its textures at.
    float mSpread;
};

/// What arrives along a bounce, whole, and the part of it the surface that drew it reflects by its
/// ambient albedo.
///
/// **The rest is reflected by the diffuse albedo**: the lights at the far hit and its glow, which
/// is lamp light bounced off another surface. The rasterizer has no term for it, and a surface
/// reflects it as it reflects a lamp.
struct Arriving
{
    vec3 mWhole;

    /// The rasterizer's ambient term: the sky the ray escaped to, which is the fill out of doors, or
    /// the far hit's `pathEnd`, which is the fill a path ends at.
    vec3 mFill;
};

/// What a bounce brings back when it reaches nothing: the fill, whole.
///
/// The glow and not the disc: the sun is already a term of its own in `GatherRun`, for the lobe as
/// well as for the diffuse half, and a bounce that found it in the sky would be the same light
/// counted twice.
///
/// **A room has nothing outside it, so a ray that got out of one brings back nothing.** The dome a
/// room draws is its fog colour standing in for the *picture* wherever a ray leaves the shell, and it
/// is far brighter than the room itself: lighting with it drew a bright band along the foot of every
/// wall. `mAmbientFromSky` is where a cell answers whether its sky is a light, and `ambientReaching`
/// reads the same field to decide how far to look for what blocks the fill.
///
/// Dimmed by the column of water over the point, on `daylightReaching`'s vertical approximation and
/// for its reason: this ray left for the sky and the sky is above, so what stands between them is the
/// depth. Without it a flooded floor reads brighter than the same floor seen from over the surface.
///
/// **The lobe's escape finds the sky a mirror shows**, `reflectedSky`, because a reflection is a
/// picture of the world: the deck, the sheets and the stars where they are, and no fill. **Not its
/// discs**: every point a bounce leaves evaluated the sun and the moons by their light samples
/// (`GatherRun`), which hold what a lobe would find of them. A branch and not a factor, because the
/// halves are the draw's own split and the reflected sky is a deck's reading the diffuse half never
/// needs.
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
/// **A Lambert surface draws what it always drew**: the cosine about the face `sampledFace` chose,
/// worth one. Nothing it reads is drawn for the lobe, so a vanilla frame traces the directions it
/// traced before there was one.
///
/// **A glossy surface draws which half, in proportion to what each sends toward the eye**: the
/// lobe at the chance `lum(Es) / (lum(Es) + lum(c_diff))`, its specular albedo against its diffuse
/// one, and each half's sample divided by its own chance, which keeps the sum unbiased. So a metal,
/// which has no diffuse half, always draws its lobe, and a rough dielectric mostly draws its
/// diffuse. The lobe's direction is `lobeSample`'s, worth its `F G2 / G1`; the diffuse one is the
/// cosine's, worth `1 - F` at its half vector — the share `GatherRun` takes off every light's diffuse
/// half, for the same reason.
///
/// **Both directions are drawn and the chance selects one.** Which half is a coin per lane, so a
/// branch on it would run both halves in nearly every warp; the select runs each once and jumps
/// nowhere.
///
/// **Off the near face only.** The lobe reflects, and the far face of a sheet transmits: `GatherRun`
/// gives a light behind a sheet no lobe and its diffuse half the whole of it, and so does this.
///
/// **One pair for either half**, `STREAM_BOUNCE`'s: only one half is kept, so the pair's spread
/// across the screen serves whichever it is.
///
/// @param cone the eye's cone at the stage that found the surface — the arms' camera for the
///        player's own arms — whose spread a reflection widens by.
BounceDraw bounceDraw(Surface surface, Gloss gloss, float face, uvec2 pixel, Cone cone)
{
    const vec2 draw = unitPair(pixel, STREAM_BOUNCE);
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

    uint lobe = randomSeed(pixelKey(pixel) + SEED_BOUNCE_LOBE);
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
    /// roughness of the lobe that reflects it, or `SPECULAR_NO_LOBE`: `CHANNEL_SPECULAR`, to which
    /// the bounce adds its own. Not multiplied by the diffuse albedo, for the reason
    /// `Bounce::mSpecular` gives.
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
    return SolidLook(
        responseOf(surface, specularModulation(gloss)), gloss.mGlossy ? surface.mRoughness : SPECULAR_NO_LOBE);
}

/// What a solid the eye found sends back before its shadow rays are answered, and the rays: what
/// `seenSolidOf` makes a `SeenSolid` of once they are (`direct.comp`).
///
/// **What reaches it from everything that is not a light is its bounce, `BounceRun`'s**, which stages
/// of their own trace and visit (`bounce.comp`, `landing.comp`): the one kernel carried every walk's
/// record at once, and the bounce's landing was a gather and a visit more.
///
/// **Everything but the rays' answers worked out first**, the gather's start and the solid's look,
/// so only these and the rays stay live through the traversals: a walk of steps kept the surface
/// and the gather's record through each of them, and the stage held 128 to 192 registers on RDNA 2.
/// The arithmetic is the walk's, in its order, so the light is the light it was, to the bit.
///
/// **The gather is split** (`GatherRun::mSplit`), at the eye's own blue noise: the sky's source and
/// the lamps go to the shadow denoiser apart, and not to the wavelet with the bounce — direct light
/// and indirect apart, as NRD has them. A lamp's shadow is sharp and a bounce is not, and one filter
/// over both blurred the one or left the other noisy. At the guild's planter at night the leaf
/// shadows smeared flat under a long history and the bounce's bright samples stood as speckle under
/// a short one; apart, the bounce takes a longer history (`ACCUMULATE_FRAMES`) and the shadows keep
/// their edge. The gather's specular half holds the lamps' lobe, which the bounce's joins for the
/// glossy filter.
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

    /// What each bit's penumbra is made of, as `stepGather` makes it of its ray's occluder: the sky's
    /// source and how far a penumbra on this surface stretches toward it; the held lamp's radius,
    /// distance and stretch, and whether a lamp was held; whether each bit is drawn among its
    /// field's sources (`drawnAmong`); and the surface's footprint they are counted in.
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
    // The gather's start, as `stepGather` makes it of a path the eye sees: rated at one.
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

    // **Split, every lamp's light as though its ray got through, and the held lamp's bit**: Heitz
    // et al. 2018's ratio estimator, the unshadowed light exact and only its visibility drawn. The
    // bit is the held lamp's, which the reservoir drew in proportion to its weight. The darkening
    // comes off the sum the bit multiplies.
    const Reservoir kept = gather.mKept;
    const DirectLight lit = gather.mLit;
    light.mRest = litSurface(surface, vec3(0.0), vec3(0.0));
    light.mSky = surface.mAlbedo * lit.mSkyDiffuse + lit.mSkySpecular;
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
    return light;
}

/// What a solid sends back once its sky's passage answered `sky` and its lamp's ray `lamp`.
///
/// **What the translucent surfaces let through is drawn into each bit**, open where its ray got
/// through and a draw falls under it: the bit's mean is then the open half times the through, and
/// the shadow denoiser filters a pane's shadow as it filters a penumbra. In the sum, one ray's
/// through was a speckle under every pane and leaf that nothing filtered.
///
/// **Each penumbra in the surface's own footprints**, which is what the denoiser's reach is counted
/// in; and the whole reach where the bit was drawn — where its own source carries less than all but
/// the floor of its field's light, since that bit is noise however hard its shadow. A bit the
/// translucency was drawn into is noise as well.
SeenSolid seenSolidOf(SolidLight light, Passage sky, Passage lamp)
{
    const bool skyPassed = light.mPassing.x < sky.mThrough;
    const bool lampPassed = light.mPassing.y < lamp.mThrough;
    const float skyRadius = skyPenumbra(light.mSkySource, sky.mOccluder) * light.mSkyStretch;
    const float lampRadius = light.mLampHeld
        ? lampPenumbraAt(light.mLampRadius, light.mLampDistance, lamp.mOccluder) * light.mLampStretch
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

/// What a solid's one bounce brings back, off the half `bounceDraw` chose, in the pieces the channels
/// keep: the bounce's stage's draw and ray (`bounce.comp`), off the surface the eye's ray or a shore's
/// bed ray found.
///
/// **One bounce, traced only from the hit the eye found**: a walk with no recursion cannot bounce a
/// bounce, and it should not — what the second hit gathers is `pathEnd`, the flat ambient that stands
/// in for the rest of the path. That is also what keeps a visit (`VisitRun`) from visiting again: the
/// water's reflections already shade through one, and a bounce inside it would have no bottom. A ray
/// that finds nothing takes `bounceEscape`, which is what makes the sky an emitter rather than a
/// backdrop: outdoors it is by far the largest source in the scene, and a surface facing it should
/// be lit by it.
///
/// **Two stages, the bounce and its landing** (`bounce.comp`, `landing.comp`): the draw and the ray
/// here, whose landing the landing's stage resolves and visits (`landingLightOf`). As one kernel, the
/// landing's visit kept every record of the bounce live through its loop, and the walks' steps a
/// warp's lanes stood at apart ran one after the other: on an RTX 4090 Laptop the stage took 255
/// registers and kept 12 of 32 lanes busy (`docs/rtx/research/2026-10-21-nvidia-stage-costs.md`).
struct BounceRun
{
    /// What the solid's lobe is divided by, as `specularModulation` packs it (`lobeModulationWord`),
    /// which the lobe's half of the bounce leaves the walk divided by, as the lamps' half is
    /// (`SeenSolid::mSpecular`). The word and not the number, so the landing's stage divides by what
    /// the lamps' half did, to the bit; only the lobe draws by it, and only off a glossy solid.
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

/// The bounce's draw off `surface`, and its ray where one is traced: the escape where the answer is
/// known without one.
///
/// @param cone the eye's cone at the stage that found the surface — the arms' camera for the
///        player's own arms — whose spread a reflection widens by.
void startBounce(inout BounceRun run, Surface surface, Cone cone, uvec2 pixel)
{
    // **The specular half the solid's own gather made**, which is the surface's alone (`glossOf`),
    // so it is made again here where no gather runs.
    const Gloss gloss = glossOf(surface);
    run.mModulationWord = lobeModulationWord(gloss);

    // A sheet bounces off either face, and `SEED_SHEET_SIDE` says why the side is not drawn from
    // the pair the direction is. Drawn on every hit and not behind a test on the transmission: the
    // sequence is its own, so a solid drawing from it moves no other, and `sampledFace` reads the
    // draw only where there is a far side.
    uint sideState = randomSeed(pixelKey(pixel) + SEED_SHEET_SIDE);
    float sided;
    const float face = sampledFace(surface.mTransmission, randomNext(sideState), sided);

    const BounceDraw drawn = bounceDraw(surface, gloss, face, pixel, cone);
    run.mDrawn = drawn;
    run.mPosition = surface.mPosition;

    // A reflection below the shading normal's horizon brings nothing back, and is not traced to
    // find that out.
    if (behindTheFace(drawn.mTowards, surface.mGeometric, face) || !(brightest(drawn.mWeight) > 0.0))
        return;

    vec3 weight = drawn.mWeight * sided;

    // **Far ground out of doors is handed the escape rather than asked whether it escaped**, which
    // is the same answer the miss below arrives at by tracing for it. `BOUNCE_REACH` says what that
    // costs and why the room is not in it.
    const vec3 fromEye = surface.mPosition - frame.mOrigin;
    if (skyLights() && surface.mGround && dot(fromEye, fromEye) > BOUNCE_REACH * BOUNCE_REACH)
    {
        run.mEscaped = bounceEscape(surface.mPosition, drawn, weight);
        return;
    }

    // Drawn last, so the side, the direction and the escape are the numbers they were. One path
    // at a rate of one: no draw reaches it, and the weight is divided by one.
    uint traced = randomSeed(pixelKey(pixel) + SEED_BOUNCE_TRACED);
    if (randomNext(traced) >= frame.mBounceRate)
        return;

    weight /= frame.mBounceRate;
    run.mWeight = weight;

    // **A ray query, and not a `traceRayEXT` the hardware could sort.** The one ray Shader
    // Execution Reordering's sources point at is this one, and sorting for it was measured: 20
    // percent slower out of doors and 30 in a room, because a bounce indoors is short and lands on
    // the same few surfaces, so there is no coherence left to recover.
    // A diffuse bounce is not drawn: it carries light, and a surface it met from behind still
    // carries it.
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
/// answered: what arrives whatever they find, what each one's answer multiplies, and the rays. The
/// landing's stage traces them and sums (`landing.comp`).
///
/// **Linear in what each ray lets through.** The landing ends a path (`VisitRun`, `GatherRun`
/// unsplit): its light is the glow, the fill times what its ambient ray let through
/// (`ambientAnswered`), and each source's unshadowed term times what its shadow ray did. So every
/// term is worked out before anything is traced, and only the three terms and the rays stay live
/// through the traversals, where the walk kept the surface, the gather's and the visit's records
/// and the sky's passage through each of them: the stage held 160 to 256 registers on RDNA 2 and
/// ran at four to six waves a SIMD. Summed in this order and not the walk's, the landing's light
/// moves by its rounding.
///
/// **Its glow is counted here, because this is the only path it takes.** Nothing gives a glowing
/// surface a lamp of its own — `EMISSIVE_INTENSITY` says what measuring that showed — so a ray that
/// lands on a mushroom cap is what carries the cap's glow back to whatever sent it.
///
/// **A lamp's own model glows to nothing**, every point a bounce leaves having evaluated the lamps
/// (`INSTANCE_LAMP_BODY`). Its lamp lights every surface around it through the model's fitting
/// (`lampAsk`) and reaches a lobe by the same sample, so a diffuse bounce that also brought back the
/// paper's glow lit the room twice — and found that glow rarely and brightly, which is a firefly — and
/// a glossy lobe that reflected the model beside the lamp's highlight showed it twice. A factor and
/// not a branch.
///
/// **One visit with the path chosen, and not one per half.** A warp whose lanes drew both halves
/// would run them one after the other. Chosen at run time, the diffuse half's hit asks the moons and
/// finds they weigh nought.
struct LandingLight
{
    /// What arrives whatever the rays find: the glow.
    vec3 mGlow;

    /// What arrives per unit of what each ray let through (`throughAnswered`): the fill, whole and
    /// as the fill alike, by the ambient ray's; the sky's source by the product of its legs'
    /// (`SkyPassageRun`); and the held lamp by its ray's. Nought where the ray is not asked.
    vec3 mFill;
    vec3 mSky;
    vec3 mLamp;

    /// The rays, any of them none: an untraced ray lets everything through, as `passageAnswered`
    /// answers one.
    RayAsk mAmbientRay;
    RayAsk mSkyRay;
    RayAsk mSkyOverRay;
    RayAsk mLampRay;
};

/// @param landing the bounce's landing (`storedLandingOf`).
/// @param lobe whether the lobe drew the bounce (`BounceDraw::mSpecular`).
/// @param weight what light arriving along the bounce is worth (`StoredBounce::mWeight`).
LandingLight landingLightOf(Surface landing, bool lobe, uvec2 pixel, vec3 weight)
{
    const bool lampBody = (instanceAt(landing.mInstance).mClass & INSTANCE_LAMP_BODY) != 0u;
    const float keep = lampBody ? 0.0 : 1.0;
    landing.mEmissiveColour *= keep;
    landing.mEmitted *= keep;

    const uint key = pixelKey(pixel);
    const uint path = lobe ? PATH_SEEN : PATH_INDIRECT;

    LandingLight light;
    light.mGlow = weight * (landing.mAlbedo * (landing.mEmissiveColour * EMISSIVE_INTENSITY) + landing.mEmitted);

    // The visit's occlusion ray, and `pathEnd`'s fill per unit of what it lets through.
    const AmbientRay ambient = ambientRayOf(landing.mPosition, landing.mNormal, landing.mGeometric,
        landing.mRounding, landing.mTransmission, key + SEED_AMBIENT_REACHING);
    const float reaching = ambient.mReaches
        ? (ambient.mRated ? ambient.mWeight / AMBIENT_EXTERIOR_RATE : ambient.mWeight)
        : 0.0;
    light.mAmbientRay = ambient.mRay;
    light.mFill = weight * (landing.mAmbientAlbedo * pathEnd(landing.mPosition, reaching));

    light.mSky = vec3(0.0);
    light.mLamp = vec3(0.0);
    light.mSkyRay = noRay();
    light.mSkyOverRay = noRay();
    light.mLampRay = noRay();

    // The gather's start, as `stepGather` makes it: drawn first, out of a sequence of its own.
    GatherRun gather = gatherRunOf(key, SEED_LAMPS_BOUNCE, path, false, false);
    float rated = 1.0;
    if (path == PATH_INDIRECT && skyLights())
    {
        if (!indirectGatherDrawn(key))
            return light;

        rated = 1.0 / INDIRECT_LIGHT_RATE;
    }

    const Gloss gloss = glossOf(landing);
    SkyPassageRun sky = skyPassageRunOf(
        frame.mSun, vec3(0.0), vec3(0.0), BentPath(0.0, vec3(0.0), 0.0, vec2(0.0)), vec2(0.0), false);
    if (startGather(gather, sky, landing, gloss, pixel))
    {
        light.mSkyRay = skyPassageFirstAsk(sky);
        light.mSkyOverRay = skyPassageOverAsk(sky);
        const SkyTerm estimate = gather.mSkyEstimate;
        light.mSky = weight * (landing.mAlbedo * ((estimate.mDiffuse - estimate.mTaken) * rated)
                                  + estimate.mSpecular * rated);
    }

    // The held lamp's term unsplit, as `stepGather` finishes it: the darkening taken off the exact
    // sum by the held lamp's estimate of it, and the lobe's share off the diffuse half.
    const Reservoir kept = gather.mKept;
    const float held = heldShare(kept);
    const vec3 lampDiffuse = kept.mRadiance * held
        * (1.0 - min(gather.mDarkening / max(kept.mUnshadowed, vec3(1e-30)), vec3(1.0)));
    const vec3 lampTaken = gloss.mGlossy ? lampDiffuse * kept.mFresnel : vec3(0.0);
    const vec3 lampSpecular = gloss.mGlossy ? kept.mSpecular * held : vec3(0.0);
    light.mLamp = weight * (landing.mAlbedo * ((lampDiffuse - lampTaken) * rated) + lampSpecular * rated);
    light.mLampRay = gatherLampAsk(gather, landing);
    return light;
}

/// What arrived, in the halves the channels keep. **The lobe takes the whole of it**: a reflection
/// is a picture of the world, and nothing in the rasterizer's sum reflects one by the ambient colour.
Bounce bounceOf(Arriving arriving, bool lobe)
{
    return lobe ? Bounce(vec3(0.0), vec3(0.0), arriving.mWhole) : Bounce(arriving.mWhole, arriving.mFill, vec3(0.0));
}

#endif
