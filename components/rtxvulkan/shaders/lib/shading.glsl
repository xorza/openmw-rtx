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

/// Which sources the shading point a ray left evaluated by their light samples, and so which of
/// their geometry emits nothing to that ray: the sun's and the moons' discs, and every lamp's model
/// (`INSTANCE_LAMP_BODY`). The sun's rule for every analytic source, **keyed by the ray's parent and
/// not by the ray**: what a lobe or a bounce that `gather` drew would find of a source, the light
/// sample at the same point holds already; a surface that evaluated no source — the water's, whose
/// legs are its whole light — leaves its rays every source's geometry whole.
const uint EVALUATED_NONE = 0u;
const uint EVALUATED_DISCS = 1u;
const uint EVALUATED_LAMPS = 2u;

/// What `gather` evaluates: every source.
const uint EVALUATED_GATHERED = EVALUATED_DISCS | EVALUATED_LAMPS;

/// The direct light a surface sends back toward the eye, in its two halves.
struct DirectLight
{
    /// What reaches the diffuse half, per unit albedo, with the share the lobe reflected at each
    /// light taken off it.
    vec3 mDiffuse;

    /// What the lobe reflects toward the eye, whole: a specular half is not multiplied by the
    /// diffuse albedo.
    vec3 mSpecular;

    /// The sky's source, kept out of the two above where `gather` was asked to split it: its diffuse
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

/// Whether a pixel keeps the second of one source's two shadowed lights' bits — the water's two
/// legs — and its penumbra with it, drawn in proportion to the luminance each adds.
///
/// **Exact in luminance on average**: the pixel's light is the sum of both times the bit kept, and
/// the bit is the second's with chance `shareB / (shareA + shareB)`, so the mean is `shareA *
/// openA + shareB * openB`. `mixShadowed` says what it costs in hue and what keeping the brighter
/// one lost. The first's where neither adds anything.
///
/// @param draw one number in `[0, 1)`, from a sequence of the caller's own.
bool keepsSecond(float shareA, float shareB, float draw)
{
    return draw * (shareA + shareB) < shareB;
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
/// `bounceLight`'s, and adding a fill here as well would count that half twice.
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
/// @param gloss the surface's specular half, `glossOf`: made once by the caller, which reports it
///        to the bounce's draw as well.
/// @param key the pixel's own, `pixelKey`, which every sequence here is drawn from with a `SEED_`
///        added: `lamps`, and `SEED_INDIRECT_LIGHT` for the bounce's rate.
/// @param lamps which draw sequence the lamp reservoir steps. **One per depth of the path**,
///        because a bounce shades a second surface and two reservoirs stepping one sequence would
///        keep correlated lamps at both ends of it.
/// @param path `PATH_SEEN` or `PATH_INDIRECT`. It decides whether the moons are asked at all, and
///        whether the rest of this is drawn at `INDIRECT_LIGHT_RATE` or spent on every hit.
/// @param split whether the sky's source and the lamps' diffuse half are handed back apart,
///        `DirectLight::mSkyDiffuse` and `mLampDiffuse` and what is beside each, for the shadow
///        denoiser (`CHANNEL_SHADOWED`, `CHANNEL_LAMPED`). **A literal at every call**:
///        what the eye sees splits — its own solid, and what the water's legs find — and the pane and
///        the bounce compose. Only with `PATH_SEEN`: the split terms do not carry the rate that
///        `PATH_INDIRECT` draws at.
/// @param pixel,blue whether the shadow rays' draws come from the tile at `pixel`
///        (`STREAM_SUN_DISC`): the eye's own split hit's. A literal at every call.
DirectLight gather(Surface surface, Gloss gloss, uint key, uint lamps, uint path, bool split, uvec2 pixel, bool blue)
{
    const vec3 position = surface.mPosition;
    const Facing facing = facingOf(surface);

    // Only the shadow rays leave from the surface the normals describe (`Surface::mLift`): what a
    // light delivers is weighed at the point shaded, the one the eye sees.
    const vec3 leaving = position + surface.mLift;

    vec3 radiance = vec3(0.0);

    // What the lobe reflects, and the share of the diffuse half's light it took. Each stays nought
    // on a surface with no specular half, and taking nought off the diffuse half is exact.
    vec3 specular = vec3(0.0);
    vec3 taken = vec3(0.0);

    DirectLight lit = DirectLight(vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), 1.0, SHADOW_PENUMBRA_CLEAR, vec3(0.0),
        1.0, SHADOW_PENUMBRA_CLEAR);

    // **Drawn before anything else and out of a sequence of its own**: the ordering below is what
    // keeps a lamp arriving in the next cell from moving the penumbra of the one already there, and
    // a draw taken from that sequence would move every one of them.
    float rated = 1.0;
    if (path == PATH_INDIRECT && skyLights())
    {
        uint rate = randomSeed(key + SEED_INDIRECT_LIGHT);
        if (randomNext(rate) >= INDIRECT_LIGHT_RATE)
            return lit;

        rated = 1.0 / INDIRECT_LIGHT_RATE;
    }

    uint state = randomSeed(key + lamps);

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
    const vec2 sunDraw = blue ? unitPair(pixel, STREAM_SUN_DISC) : sunHashed;
    const float skyPick = blue ? randomAt(pixel, STREAM_SKY_PICK) : skyHashed;
    const vec2 lampDraw = blue ? unitPair(pixel, STREAM_LAMP_DISC) : lampHashed;

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
    // to — no better resolved for it. Masser subtends thirty-five times the sun's angle, so the cone
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
    const bool lunar = HAS_MOONS && path == PATH_SEEN;
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
    float skyCarried = 0.0;
    float skyThrough = 1.0;
    if (pick.mTotal > 0.0)
    {
        const SkyChoice picked = pick.mIndex == 0u ? sun : (pick.mIndex == 1u ? masser : secunda);
        skyCarried = picked.mLight.mWeight;

        // Split, the rays' own bit is handed back and the rest of the estimate is made as though
        // they got through: the product of the two is the estimate unsplit, and the bit is what a
        // denoiser filters in its place.
        // The shadow rays leave from where every ray off the surface leaves, and the light arrives
        // where the surface is: one refraction, a bent path to each.
        const SunUnderWater bent = sunUnderWater(picked.mSky.mDirection);
        const Passage passage
            = skyPassageThrough(picked.mSky, leaving, stepOf(surface), bentPathAt(leaving, bent), sunDraw, split);

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
            lit.mSkyDiffuse = every.mDiffuse - every.mTaken;
            lit.mSkySpecular = every.mSpecular;
            lit.mSkyOpen = passage.mOpen;
            skyThrough = passage.mThrough;
            lit.mSkyPenumbra = skyPenumbra(picked.mSky, passage.mOccluder) * receiverStretch(picked.mCosine);
        }
        else
        {
            const SkyTerm minors = joinedTerms(
                joinedTerms(scaledTerm(sunTerm, minorWeight(sun.mLight.mWeight, minor)), masserTerm,
                    minorWeight(masser.mLight.mWeight, minor)),
                secundaTerm, minorWeight(secunda.mLight.mWeight, minor));
            const SkyTerm estimate = joinedTerms(minors, pickedTerm, pick.mWhole ? 1.0 : 1.0 / pick.mChance);
            const float seen = passage.mOpen * passage.mThrough;
            radiance += estimate.mDiffuse * seen;
            specular += estimate.mSpecular * seen;
            taken += estimate.mTaken * seen;
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
    // legs among it (`lightAtPathEnd`), so a water pixel walks the cell once a leg.
    weighLamps(kept, state, position, facing, INV_PI, gloss, surface.mLampLit, split ? 0u : frame.mLampCandidates);
    kept.mFrom = leaving;

    const Passage lampPass = kept.mWeight > 0.0 ? lampPassage(kept, stepOf(surface), lampDraw, split) : Passage(1.0, 1.0, SHADOW_PENUMBRA_CLEAR);
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

    // **The lamps that take light away take it off the lamps' exact sum and no further**, floored at
    // nought as the rasterizer clamps its lighting: the sun, the sky and the bounce stay whole.
    // Split, the sum is what the bit multiplies. Unsplit, the clamped sum is scaled by the held
    // lamp's estimate of how much of it got through: clamped on that one-lamp estimate instead,
    // every pixel the held lamp outshone the darkening kept light the mean did not have, and the
    // ground near a negative lamp came out brighter than its own lamps leave it.
    // Walked only where a lamp was held. With none, every lamp weighed nought, so its light is
    // nought or the albedo that would show it is (`surfaceCandidate`): unsplit the sum below is
    // nought whatever is taken off it, and split it reaches the picture only times that albedo.
    const vec3 darkening = darkeningAt(position, facing, INV_PI, surface.mLampLit && kept.mWeight > 0.0);
    // `1 - min(d / u, 1)` is `max(u - d, 0) / u`, and exactly one where nothing darkens.
    const vec3 lampDiffuse = split
        ? max(lampsArriving - darkening, vec3(0.0))
        : lampsArriving * (1.0 - min(darkening / max(kept.mUnshadowed, vec3(1e-30)), vec3(1.0)));

    if (gloss.mGlossy)
        specular += kept.mSpecular * (held * lampSeen);

    if (split)
    {
        // **Each source's bit apart, for a field of its own each** (`CHANNEL_SHADOWED` says what one
        // bit for both cost). **What the translucent surfaces let through is drawn into each bit**,
        // open where its ray got through and a draw falls under it: the bit's mean is then the open
        // half times the through, the product the sum carried before, and the shadow denoiser
        // filters a pane's shadow as it filters a penumbra. In the sum, one ray's through was a
        // speckle under every pane and leaf that nothing filtered.
        uint passing = randomSeed(key + SEED_SHADOW_THROUGH);
        const bool skyPassed = randomNext(passing) < skyThrough;
        const bool lampPassed = randomNext(passing) < lampPass.mThrough;
        lit.mSkyOpen *= skyPassed ? 1.0 : 0.0;
        lit.mLampDiffuse = lampDiffuse;
        lit.mLampOpen = lampPass.mOpen * (lampPassed ? 1.0 : 0.0);

        // In the surface's own footprints, which is what the denoiser's reach is counted in; and the
        // whole reach where the bit was drawn — where its own source carries less than all but the
        // floor of its field's light, since that bit is noise however hard its shadow. As products,
        // for the reason `pickByWeight` compares against weights and not quotients. A bit the
        // translucency was drawn into is noise as well.
        const float whole = 1.0 - frame.mShadowFloor;
        const bool skyDrawn = skyCarried < whole * skyWeight || skyThrough < 1.0;
        const bool lampDrawn = kept.mWeight < whole * kept.mTotal || lampPass.mThrough < 1.0;
        const float lampRadius = kept.mWeight > 0.0 ? lampPenumbra(kept, lampPass.mOccluder)
                * receiverStretch(litCosine(facing, normalize(lightAt(kept.mLamp).mPosition - position)))
                                                     : SHADOW_PENUMBRA_CLEAR;
        lit.mSkyPenumbra = penumbraIn(skyDrawn, lit.mSkyPenumbra, surface.mFootprint);
        lit.mLampPenumbra = penumbraIn(lampDrawn, lampRadius, surface.mFootprint);
    }
    else
    {
        radiance += lampDiffuse;
        if (gloss.mGlossy)
            taken += lampDiffuse * kept.mFresnel;
    }

    lit.mDiffuse = (radiance - taken) * rated;
    lit.mSpecular = specular * rated;
    return lit;
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

/// What the lobe's light is divided by before the glossy filter averages it, and the composite
/// multiplies back: `gloss`'s split-sum specular albedo (`Gloss::mAlbedo`). Every channel a filter
/// averages holds light per unit of the albedo the composite puts back, so a history blended over a
/// replacer's speckled reflectance keeps the speckle sharp, as the bounce's demodulation keeps a
/// texture. Held at `SPECULAR_ALBEDO_FLOOR` from below, which says why it is
/// a floor, and one where there is no lobe.
///
/// **Rounded to what the payload carries before anything is divided by it** (`packRgb9e5`, whose
/// every value a half holds as well): the light divided by this and the channel the composite
/// multiplies by are one number, so the two meet to the rounding of a product. The floor survives
/// the rounding as a step of the shared exponent's mantissa, never nought, while every channel of
/// the albedo rounds below four.
vec3 specularModulation(Gloss gloss)
{
    const vec3 rounded = unpackRgb9e5(packRgb9e5(max(gloss.mAlbedo, vec3(SPECULAR_ALBEDO_FLOOR))));
    return mix(vec3(1.0), rounded, bvec3(gloss.mGlossy));
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
/// @param diffuse what reaches the diffuse half, per unit albedo: the lights `gather` found and what
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

/// A surface's light with what `gather` split off kept apart, `rest` beside it.
SplitLight splitLightOf(Surface surface, DirectLight lit, vec3 rest)
{
    return SplitLight(rest,
        Shadowed(surface.mAlbedo * lit.mSkyDiffuse + lit.mSkySpecular, lit.mSkyOpen, lit.mSkyPenumbra),
        Shadowed(surface.mAlbedo * lit.mLampDiffuse, lit.mLampOpen, lit.mLampPenumbra));
}

/// Two of one source's shadowed lights that one pixel shows, `mix(a, b, t)`, with one bit between
/// them.
///
/// **The bit is one of the two, drawn in proportion to the luminance each adds.** A pixel has one
/// bit of each source for the shadow denoiser to filter, and what it filters to is then the two
/// visibilities weighed by those shares — under the sum of both, which makes the pixel's luminance
/// exact on average. What is not exact is its hue, where the two differ in colour and in
/// visibility: the water's legs do, since only one of them crosses the water. At the pond under a
/// canopy that `-1,-9` looks at, the converged picture stands within 0.6 of a code of the exact one
/// in every channel.
///
/// **Keeping the brighter's bit and composing the other was exact and lost.** The other term keeps
/// its own ray's noise at its share of the pixel, and where the Fresnel term is near a half both
/// shares are large: the same pond's error against a reference fell from 17 codes to 8.4, and to 5.3
/// drawn. And it is not exact once filtered either, because a neighbour that kept the other's bit is
/// averaged in.
///
/// **A bit drawn between two that both add light is noise however hard either shadow is**, so its
/// penumbra is `SHADOW_PENUMBRA_DRAWN`, as `gather`'s is for a bit drawn among a field's sources.
///
/// @param draw one number in `[0, 1)`, from a sequence of the caller's own.
Shadowed mixShadowed(Shadowed a, Shadowed b, float t, float draw)
{
    const vec3 fromA = a.mLight * (1.0 - t);
    const vec3 fromB = b.mLight * t;

    const float shareA = dot(fromA, LUMINANCE_WEIGHTS);
    const float shareB = dot(fromB, LUMINANCE_WEIGHTS);
    const bool second = keepsSecond(shareA, shareB, draw);
    const bool drawn = shareA > 0.0 && shareB > 0.0;
    return Shadowed(fromA + fromB, second ? b.mOpen : a.mOpen,
        drawn ? SHADOW_PENUMBRA_DRAWN : (second ? b.mPenumbra : a.mPenumbra));
}

/// Two split lights that one pixel shows, `mix(a, b, t)`: the rest mixed, and each source's light
/// with one bit between them (`mixShadowed`), by a draw of its own.
SplitLight mixSplit(SplitLight a, SplitLight b, float t, float skyDraw, float lampDraw)
{
    return SplitLight(mix(a.mRest, b.mRest, t), mixShadowed(a.mSky, b.mSky, t, skyDraw),
        mixShadowed(a.mLamps, b.mLamps, t, lampDraw));
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

/// How much of the ambient a surface can see, as one cosine-weighted sample of its own hemisphere.
///
/// **The same integral the bounce already samples, one level further down.** A ray the eye found
/// gathers a real hemisphere and is occluded by whatever it hits; what *that* ray landed on was
/// handed the whole ambient whatever stood over it, so a point in a hollow was lit as though nothing
/// stood over it. This is the missing half, and it is a visibility ray rather than a bounce: nothing
/// is shaded at the far end, only asked whether there is one.
///
/// **One sample, and it is binary — `rate` of a sample out of doors.** That is as noisy as a single
/// sample can be, and it multiplies a term already carried by one, so it rides the same filter where
/// there is one. The estimator stays unbiased at any rate, which a cheaper guess would not be. See
/// `AMBIENT_EXTERIOR_RATE` and `AMBIENT_UNFILTERED_RATE`.
///
/// **A sheet asks both faces**, by `sampledFace`: a leaf in the open sees the sky over its back as
/// well, at `transmission` of what it sees over its front, so what reaches it runs to
/// `1 + transmission` of a solid's whole. The side is drawn after the direction and only where
/// there is one to draw, so a solid's sequence is what it was.
///
/// **A direction behind the surface's own triangle reaches nothing**, by `behindTheFace`: there is
/// no ambient inside a wall.
///
/// @param normal the hemisphere's axis — a surface's shading normal, or nothing at all for a froxel
///        of the air, which is `weighLamps`' contract and means the same thing here: the air has no
///        side to face away from, so what stands over it is asked over the whole sphere rather than
///        over a hemisphere.
/// @param plane the surface's own triangle, as `behindTheFace` takes it and as the ray leaves it
///        (`leaveSurface`): nought for a froxel, which starts where it is.
/// @param rounding how far `position` can stand off that triangle, `Surface::mRounding`.
/// @param rate what share of the rays out of doors are traced: `AMBIENT_EXTERIOR_RATE` where a
///        filter takes the answer, and `AMBIENT_UNFILTERED_RATE` where none does.
float ambientReaching(vec3 position, vec3 normal, vec3 plane, float rounding, float transmission, uint seed, float rate)
{
    uint state = randomSeed(seed);
    const vec2 draw = vec2(randomNext(state), randomNext(state));

    float weight = 1.0;
    vec3 towards;

    if (dot(normal, normal) > 0.0)
    {
        const float face = sampledFace(transmission, transmission > 0.0 ? randomNext(state) : 0.0, weight);
        towards = cosineDirection(normal * face, draw);

        if (behindTheFace(towards, plane, face))
            return 0.0;
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
        return weight * ambientThrough(leaveSurface(position, plane * rounding, towards), towards, ROOM_FILL_REACH);

    // Drawn last, so a solid's direction and a sheet's side are the numbers they were.
    if (randomNext(state) >= rate)
        return 0.0;

    return weight * ambientThrough(leaveSurface(position, plane * rounding, towards), towards, frame.mReach) / rate;
}

/// `ambientReaching` for a surface a path ends at — the bounce's far hit, a water leg's, a pane.
/// **The fill `pathEnd` gives is the bounces nobody traces.**
float surfaceAmbient(Surface hit, uint seed, float rate)
{
    return ambientReaching(hit.mPosition, hit.mNormal, hit.mGeometric, hit.mRounding, hit.mTransmission, seed, rate);
}

/// What a surface a path ends at sends back, with the fill apart from the lights.
struct PathEnd
{
    /// `gather`'s light through `litSurface`, the glow with it, and the shadowed sources apart where
    /// the caller split them off.
    SplitLight mLit;

    /// The fill's two factors: `pathEnd` under the surface's one occlusion ray, and the ambient
    /// albedo that reflects it.
    vec3 mAmbient;
    vec3 mAmbientAlbedo;
};

/// What a surface a path ends at sends back: the lights `gather` finds, and `pathEnd`, dimmed by one
/// occlusion ray of its own, for the rest of the path.
///
/// **One statement of the tail the paths share**: the far end of a water ray, and the hit the eye's
/// bounce found. A pane ends its path in `shadePane`, the same terms kept apart for its filter.
///
/// **The lights by the albedo and the fill by the ambient albedo**, as the rasterizer lights a
/// fragment `texture × (D × lit + A × ambient)`: `pathEnd` stands in for the rest of the path, which
/// is the ambient's place in the rasterizer's sum.
///
/// **The water's legs split the shadowed sources off**, because what they find is what the pixel
/// shows: under a canopy, one shadow ray a pixel speckles a reflection that the same rock seen
/// directly hands to the shadow denoiser. The bounce composes it, since the wavelet filters its
/// whole light.
///
/// @param key the pixel's own, `pixelKey`, and `ambient` and `lamps` the `SEED_` the occlusion ray
///        and the lamp reservoir draw from with it. Two, for the reason `SEED_AMBIENT_REACHING`
///        gives.
/// @param split as `gather` takes it, and a literal at every call for the same reason.
/// @param ambientRate as `ambientReaching` takes it.
PathEnd lightAtPathEnd(Surface hit, uint key, uint ambient, uint lamps, uint path, bool split, float ambientRate)
{
    const float reaching = surfaceAmbient(hit, key + ambient, ambientRate);
    const DirectLight lit = gather(hit, glossOf(hit), key, lamps, path, split, uvec2(0u), false);

    return PathEnd(splitLightOf(hit, lit, litSurface(hit, lit.mDiffuse, lit.mSpecular)),
        pathEnd(hit.mPosition, reaching), hit.mAmbientAlbedo);
}

/// What a path end sends back by its ambient albedo.
vec3 fillOf(PathEnd end)
{
    return end.mAmbientAlbedo * end.mAmbient;
}

/// A path end's light with the fill joined to the rest.
///
/// **One rounding, by an explicit `fma`.** The pinned module fuses a product that one add reads and
/// no other (`pinFloatArithmetic`), and a bounce reads the fill twice, joined and apart: written as
/// a sum, the bounce's whole rounds twice where a water leg's rounds once.
SplitLight joinedLight(PathEnd end)
{
    return SplitLight(fma(end.mAmbientAlbedo, end.mAmbient, end.mLit.mRest), end.mLit.mSky, end.mLit.mLamps);
}

/// `lightAtPathEnd` with the fill joined to the lights, for a path whose end nothing reflects by a
/// second albedo.
SplitLight shadeAtPathEnd(
    Surface hit, uint key, uint ambient, uint lamps, uint path, bool split, float ambientRate)
{
    return joinedLight(lightAtPathEnd(hit, key, ambient, lamps, path, split, ambientRate));
}

/// What a see-through layer sends back, in the pieces the pane filter takes apart.
struct SeenPane
{
    /// What it glows with, which nothing drew: `litSurface` with no light arriving.
    vec3 mGlow;

    /// What a path end drew for it, whole — `gather`'s diffuse half by the albedo, and `pathEnd`
    /// under its one occlusion ray by the ambient albedo — and what its lobe reflects of that, which
    /// the launch composes unfiltered (`PaneStack::mDrawn`). **Whole and not per unit albedo**,
    /// because the two halves take two albedos, and the pane filter averages a layer over time
    /// alone, so nothing needs the light apart from them.
    vec3 mDrawn;
    vec3 mSpecular;

    SurfaceResponse mResponse;
};

/// `shadeAtPathEnd` for a layer the eye looks through, with what was drawn kept apart from what was
/// not: the pane filter averages the one over time, and the glow is exact as it stands.
///
/// **The same terms, so a pane composed from these is the pane `shadeAtPathEnd` shades**: the glow
/// plus the drawn light plus the lobe is its light, to its rounding.
///
/// **At `AMBIENT_EXTERIOR_RATE`**, because the pane filter takes what it draws, as the glossy filter
/// takes what a lobe's path end draws at the same rate.
///
/// @param key,ambient,lamps as `shadeAtPathEnd` takes them.
SeenPane shadePane(Surface hit, uint key, uint ambient, uint lamps)
{
    const float reaching = surfaceAmbient(hit, key + ambient, AMBIENT_EXTERIOR_RATE);
    const DirectLight lit = gather(hit, glossOf(hit), key, lamps, PATH_SEEN, false, uvec2(0u), false);

    return SeenPane(litSurface(hit, vec3(0.0), vec3(0.0)),
        hit.mAlbedo * lit.mDiffuse + hit.mAmbientAlbedo * pathEnd(hit.mPosition, reaching), lit.mSpecular,
        responseOf(hit, vec3(1.0)));
}

/// What one bounce brings back, in the two halves `shadeSolid` hands on apart.
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
/// The glow and not the disc: the sun is already a term of its own in `gather`, for the lobe as
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
/// picture of the world: the deck, the sheets and the stars where they are, and no fill. Its discs
/// as `evaluated` says. A branch and not a factor, because the halves are the draw's own split and
/// the reflected sky is a deck's reading the diffuse half never needs.
///
/// @param evaluated which sources the point the bounce left evaluated (`EVALUATED_DISCS`).
Arriving bounceEscape(vec3 position, BounceDraw drawn, vec3 weight, uint evaluated)
{
    if (!skyLights())
        return Arriving(vec3(0.0), vec3(0.0));

    const vec3 sky = drawn.mSpecular
        ? reflectedSky(position, drawn.mTowards, 0.5 * drawn.mSpread, (evaluated & EVALUATED_DISCS) == 0u)
        : skyGlow(drawn.mTowards);
    const vec3 daylight = daylightReaching(position);
    const vec3 escaped = weight * sky * daylight;
    return Arriving(escaped, escaped);
}

/// What leaves the surface a bounce landed on toward the surface that drew it, unweighted: the
/// end of the path, whole, and its fill.
///
/// **A lamp's own model glows to nothing whose parent evaluated the lamps** (`EVALUATED_LAMPS`,
/// `INSTANCE_LAMP_BODY`). Its lamp lights every surface around it through the model's fitting
/// (`lampPassage`) and reaches a lobe by the same sample, so a diffuse bounce that also brought back
/// the paper's glow lit the room twice — and found that glow rarely and brightly, which is a
/// firefly — and a glossy lobe that reflected the model beside the lamp's highlight showed it twice.
/// A factor and not a branch.
///
/// @param evaluated which sources the point the bounce left evaluated (`EVALUATED_LAMPS`).
Arriving bounceLanding(Surface landed, uint key, uint ambient, uint lamps, uint path, uint evaluated)
{
    const bool lampBody = (instanceAt(landed.mInstance).mClass & INSTANCE_LAMP_BODY) != 0u;
    const float keep = (evaluated & EVALUATED_LAMPS) != 0u && lampBody ? 0.0 : 1.0;
    Surface hit = landed;
    hit.mEmissiveColour *= keep;
    hit.mEmitted *= keep;

    const PathEnd end = lightAtPathEnd(hit, key, ambient, lamps, path, false, AMBIENT_EXTERIOR_RATE);
    return Arriving(composed(joinedLight(end)), fillOf(end));
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
/// cosine's, worth `1 - F` at its half vector — the share `gather` takes off every light's diffuse
/// half, for the same reason.
///
/// **Both directions are drawn and the chance selects one.** Which half is a coin per lane, so a
/// branch on it would run both halves in nearly every warp; the select runs each once and jumps
/// nowhere.
///
/// **Off the near face only.** The lobe reflects, and the far face of a sheet transmits: `gather`
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

/// What arrives along a bounce's direction, times `weight`: the sky it escapes to, or the surface it
/// lands on, shaded as the end of the path.
///
Arriving bounceArriving(Surface surface, BounceDraw drawn, vec3 weight, uvec2 pixel)
{
    // **Far ground out of doors is handed the escape rather than asked whether it escaped**, which
    // is the same answer the miss below arrives at by tracing for it. `BOUNCE_REACH` says what that
    // costs and why the room is not in it.
    const vec3 fromEye = surface.mPosition - frame.mOrigin;
    if (skyLights() && surface.mGround && dot(fromEye, fromEye) > BOUNCE_REACH * BOUNCE_REACH)
        return bounceEscape(surface.mPosition, drawn, weight, EVALUATED_GATHERED);

    // Drawn last, so the side, the direction and the escape are the numbers they were. One path
    // at a rate of one: no draw reaches it, and the weight is divided by one.
    uint traced = randomSeed(pixelKey(pixel) + SEED_BOUNCE_TRACED);
    if (randomNext(traced) >= frame.mBounceRate)
        return Arriving(vec3(0.0), vec3(0.0));

    weight /= frame.mBounceRate;

    // **An inline query inside the closest-hit shader, and not a second launch-side trace.** The
    // one ray Shader Execution Reordering's sources point at is this one, and sorting for it was
    // measured: 20 percent slower out of doors and 30 in a room, because a bounce indoors is short
    // and lands on the same few surfaces, so there is no coherence left to recover.
    // A diffuse bounce is not drawn: it carries light, and a surface it met from behind still
    // carries it.
    const Surface hit = trace(
        WorldRay(leaveSurface(surface.mPosition, stepOf(surface), drawn.mTowards), drawn.mTowards), 0.0,
        Cone(surface.mFootprint, drawn.mSpread), solidMask(frame.mRayMask), drawn.mSpecular);

    if (!hit.mHit)
        return bounceEscape(surface.mPosition, drawn, weight, EVALUATED_GATHERED);

    // **Its glow is counted here, because this is the only path it takes.** Nothing gives a glowing
    // surface a lamp of its own — `EMISSIVE_INTENSITY` says what measuring that showed — so a ray
    // that lands on a mushroom cap is what carries the cap's glow back to whatever sent it. A
    // lamp's own model is the exception `bounceLanding` makes: its lamp carries its glow already.
    //
    // **One call with the path chosen, and not one per half.** Written out twice, the whole end of
    // the path is two copies, and a warp whose lanes drew both halves runs them one after the
    // other. Chosen at run time, the diffuse half's hit asks the moons and finds they weigh nought.
    const Arriving left = bounceLanding(hit, pixelKey(pixel), SEED_AMBIENT_REACHING, SEED_LAMPS_BOUNCE,
        drawn.mSpecular ? PATH_SEEN : PATH_INDIRECT, EVALUATED_GATHERED);

    return Arriving(weight * left.mWhole, weight * left.mFill);
}

/// What reaches a surface from everything that is not a light: one bounce, off the half
/// `bounceDraw` chose.
///
/// **Traced only from the hit the eye found.** A shader with no recursion cannot bounce a bounce, and
/// it should not: what the second hit gathers is `pathEnd`, the flat ambient that stands in for the
/// rest of the path. That is also what keeps `lightAtPathEnd` from calling itself — the water's
/// reflections already shade through it, and a bounce inside it would have no bottom.
///
/// A ray that finds nothing takes `bounceEscape`, which is what makes the sky an emitter rather than
/// a backdrop: outdoors it is by far the largest source in the scene, and a surface facing it should
/// be lit by it.
///
/// @param gloss the surface's specular half, `glossOf`.
/// @param cone as `bounceDraw` takes it.
Bounce bounceLight(Surface surface, Gloss gloss, uvec2 pixel, Cone cone)
{
    // A sheet bounces off either face, and `SEED_SHEET_SIDE` says why the side is not drawn from
    // the pair the direction is. Drawn on every hit and not behind a test on the transmission: the
    // sequence is its own, so a solid drawing from it moves no other, and `sampledFace` reads the
    // draw only where there is a far side.
    uint sideState = randomSeed(pixelKey(pixel) + SEED_SHEET_SIDE);
    float sided;
    const float face = sampledFace(surface.mTransmission, randomNext(sideState), sided);

    const BounceDraw drawn = bounceDraw(surface, gloss, face, pixel, cone);

    // A reflection below the shading normal's horizon brings nothing back, and is not traced to
    // find that out.
    if (behindTheFace(drawn.mTowards, surface.mGeometric, face) || !(brightest(drawn.mWeight) > 0.0))
        return Bounce(vec3(0.0), vec3(0.0), vec3(0.0));

    const Arriving arriving = bounceArriving(surface, drawn, drawn.mWeight * sided, pixel);

    // **The lobe takes the whole of it**: a reflection is a picture of the world, and nothing in the
    // rasterizer's sum reflects one by the ambient colour.
    return drawn.mSpecular ? Bounce(vec3(0.0), vec3(0.0), arriving.mWhole)
                           : Bounce(arriving.mWhole, arriving.mFill, vec3(0.0));
}

/// What a solid the eye found sends back, in the channels' pieces.
struct SeenSolid
{
    /// Everything resolved but what a filter takes, the glow, in `mRest`, and what the sky's source
    /// and the lamps add and whether each one's ray got through: `CHANNEL_SHADOWED` and
    /// `CHANNEL_LAMPED`.
    SplitLight mLight;

    /// The diffuse light the wavelet filters, per unit albedo: the one bounce, and the share of it
    /// that is the fill, `Bounce::mFill`.
    vec3 mBounce;
    vec3 mFill;

    /// What the solid is in the filter's terms.
    SurfaceResponse mResponse;

    /// What the lobe reflects of the lamps and of the one bounce, per unit of `mResponse.mSpecular`,
    /// and the perceptual roughness of the lobe that reflects it, or `SPECULAR_NO_LOBE`:
    /// `CHANNEL_SPECULAR`. Not multiplied by the diffuse albedo, for the reason `Bounce::mSpecular`
    /// gives.
    vec3 mSpecular;
    float mRoughness;
};

/// What a solid the eye found is: its direct light, the one bounce it gathers, the shadowed sources
/// apart for the shadow denoiser, the lobe's light apart for the glossy filter, and what it is in the
/// filter's terms.
///
/// **One statement of what a ground pixel is, used twice** — for the hit itself, and for the bed
/// under a waterline pixel, which is that ground and has to be shaded exactly as it. Written twice
/// is how the two would come to disagree.
///
/// @param cone as `bounceDraw` takes it.
SeenSolid shadeSolid(Surface hit, uvec2 pixel, Cone cone)
{
    const Gloss gloss = glossOf(hit);
    const DirectLight lit = gather(hit, gloss, pixelKey(pixel), SEED_LAMPS_EYE, PATH_SEEN, true, pixel, true);
    const Bounce bounced = bounceLight(hit, gloss, pixel, cone);

    // **The lamps' diffuse half goes to the shadow denoiser with the sky's, and not to the wavelet
    // with the bounce**: direct light and indirect apart, as NRD has them. A lamp's shadow is sharp
    // and a bounce is not, and one filter over both blurred the one or left the other noisy. At the
    // guild's planter at night the leaf shadows smeared flat under a long history and the bounce's
    // bright samples stood as speckle under a short one; apart, the bounce takes a longer history
    // (`ACCUMULATE_FRAMES`) and the shadows keep their edge. Split, `gather`'s specular half holds
    // the lamps' lobe, which joins the bounce's for the glossy filter.
    SeenSolid seen;
    seen.mLight = splitLightOf(hit, lit, litSurface(hit, vec3(0.0), vec3(0.0)));
    seen.mBounce = bounced.mDiffuse;
    seen.mFill = bounced.mFill;
    const vec3 modulation = specularModulation(gloss);
    seen.mSpecular = (lit.mSpecular + bounced.mSpecular) / modulation;
    seen.mRoughness = gloss.mGlossy ? hit.mRoughness : SPECULAR_NO_LOBE;
    seen.mResponse = responseOf(hit, modulation);
    return seen;
}

#endif
