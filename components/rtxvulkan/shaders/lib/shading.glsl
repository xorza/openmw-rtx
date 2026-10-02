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
#include "frame.glsl"
#include "gloss.glsl"
#include "lights.glsl"
#include "random.glsl"
#include "records.glsl"
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

    /// The sky's source, kept out of the two above where `gather` was asked to split it: its diffuse
    /// half per unit albedo and net of the lobe's share, and its lobe whole, each as though its rays
    /// got through — and whether they did, one or nought. Nought and one where it was not split.
    vec3 mSkyDiffuse;
    vec3 mSkySpecular;
    float mSkyOpen;
};

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
/// @param split whether the sky's source is handed back apart, `DirectLight::mSkyDiffuse` and the
///        two beside it, for the shadow denoiser (`CHANNEL_SUNLIT`). **A literal at every call**:
///        what the eye sees splits — its own solid, and what the water's legs find — and the pane and
///        the bounce compose. Only with `PATH_SEEN`: the split terms do not carry the rate that
///        `PATH_INDIRECT` draws at.
DirectLight gather(Surface surface, Gloss gloss, uint key, uint lamps, uint path, bool split)
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

    DirectLight lit = DirectLight(vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), 1.0);

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
    const vec2 sunDraw = vec2(randomNext(state), randomNext(state));
    const float skyPick = randomNext(state);
    const vec2 lampDraw = vec2(randomNext(state), randomNext(state));

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

    const WeightedPick pick
        = pickByWeight(sun.mLight.mWeight, masser.mLight.mWeight, secunda.mLight.mWeight, skyPick);
    if (pick.mTotal > 0.0)
    {
        const SkyChoice picked = pick.mIndex == 0u ? sun : (pick.mIndex == 1u ? masser : secunda);

        // Split, the rays' own bit is handed back and the rest of the estimate is made as though
        // they got through: the product of the two is the estimate unsplit, and the bit is what a
        // denoiser filters in its place.
        const Passage passage = skyPassageThrough(picked.mSky, leaving, sunDraw);
        const float skySeen = split ? passage.mThrough : passage.mOpen * passage.mThrough;
        const vec3 water = lightThroughWater(position, picked.mSky.mDirection, surface.mFootprint);
        const vec3 skyArriving = picked.mSky.mIrradiance * water;
        const float skyLit = picked.mCosine * INV_PI * skySeen;
        const vec3 skyDiffuse = skyArriving * (pick.mWhole ? skyLit : skyLit / pick.mChance);
        const vec3 skySpecular = gloss.mGlossy
            ? water * picked.mLight.mSpecular * (pick.mWhole ? skySeen : skySeen / pick.mChance)
            : vec3(0.0);
        const vec3 skyTaken = gloss.mGlossy ? skyDiffuse * picked.mLight.mFresnel : vec3(0.0);

        if (split)
        {
            lit.mSkyDiffuse = skyDiffuse - skyTaken;
            lit.mSkySpecular = skySpecular;
            lit.mSkyOpen = passage.mOpen;
        }
        else
        {
            radiance += skyDiffuse;
            specular += skySpecular;
            taken += skyTaken;
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
    weighLamps(kept, state, position, facing, INV_PI, gloss);
    kept.mFrom = leaving;

    // **The lamps that take light away take it off the lamps' term and no further**, floored at
    // nought as the rasterizer clamps its lighting: the sun, the sky and the bounce stay whole.
    float lampShare;
    const vec3 lampDiffuse
        = max(lampsThrough(kept, lampDraw, lampShare) - darkeningAt(position, facing, INV_PI), vec3(0.0));
    radiance += lampDiffuse;

    if (gloss.mGlossy)
    {
        specular += kept.mSpecular * lampShare;
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

/// What a surface is in the filter's and the composite's terms: its shading normal and its diffuse
/// albedo, whether or not it has a specular half.
SurfaceResponse responseOf(Surface surface)
{
    return SurfaceResponse(packSurfaceNormal(surface.mNormal), surface.mAlbedo);
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

/// What a surface sends back, with the sky's source kept apart: `CHANNEL_SUNLIT`'s two halves
/// beside everything else.
struct SplitLight
{
    /// Everything but the sky's source.
    vec3 mRest;

    /// What the sky's source adds as though its rays got through, albedo and lobe included, and
    /// whether they did, one or nought. Nought and one where nothing split it off.
    vec3 mSunlit;
    float mSunOpen;
};

/// The whole of a split light, with the sky's source as its own ray found it.
vec3 composed(SplitLight light)
{
    return light.mRest + light.mSunlit * light.mSunOpen;
}

/// What the sky's source adds to a surface as though its rays got through, out of what `gather`
/// split off.
vec3 skyLight(Surface surface, DirectLight lit)
{
    return surface.mAlbedo * lit.mSkyDiffuse + lit.mSkySpecular;
}

/// Two split lights that one pixel shows, `mix(a, b, t)`, with one bit between them.
///
/// **The bit is one of the two, drawn in proportion to the luminance each source adds.** A pixel
/// has one bit for the shadow denoiser to filter, and what it filters to is then the two
/// visibilities weighed by those shares — under the sum of both sources, which makes the pixel's
/// luminance exact on average. What is not exact is its hue, where the two sources differ in colour
/// and in visibility: the water's legs do, since only one of them crosses the water. At the pond
/// under a canopy that `-1,-9` looks at, the converged picture stands within 0.6 of a code of the
/// exact one in every channel.
///
/// **Keeping the brighter source's bit and composing the other was exact and lost.** The other
/// term keeps its own ray's noise at its share of the pixel, and where the Fresnel term is near a
/// half both shares are large: the same pond's error against a reference fell from 17 codes to 8.4,
/// and to 5.3 drawn. And it is not exact once filtered either, because a neighbour that kept the
/// other source's bit is averaged in.
///
/// @param draw one number in `[0, 1)`, from a sequence of the caller's own.
SplitLight mixSplit(SplitLight a, SplitLight b, float t, float draw)
{
    const vec3 fromA = a.mSunlit * (1.0 - t);
    const vec3 fromB = b.mSunlit * t;
    const float shareA = dot(fromA, LUMINANCE_WEIGHTS);
    const float shareB = dot(fromB, LUMINANCE_WEIGHTS);

    return SplitLight(
        mix(a.mRest, b.mRest, t), fromA + fromB, draw * (shareA + shareB) < shareB ? b.mSunOpen : a.mSunOpen);
}

/// `litSurface` over the whole of `gather`'s light, with the sky's source apart where `split` asks.
///
/// @param gloss the surface's specular half, `glossOf`.
/// @param incoming what arrives from everything that is not a light, `pathEnd` at the hit a
///        hemisphere found.
/// @param split as `gather` takes it, and a literal at every call for the same reason.
SplitLight shadeSurface(Surface surface, Gloss gloss, vec3 incoming, uint key, uint lamps, uint path, bool split)
{
    const DirectLight lit = gather(surface, gloss, key, lamps, path, split);
    return SplitLight(
        litSurface(surface, incoming + lit.mDiffuse, lit.mSpecular), skyLight(surface, lit), lit.mSkyOpen);
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
/// @param plane the surface's own triangle, as `behindTheFace` takes it.
/// @param rate what share of the rays out of doors are traced: `AMBIENT_EXTERIOR_RATE` where a
///        filter takes the answer, and `AMBIENT_UNFILTERED_RATE` where none does.
float ambientReaching(vec3 position, vec3 normal, vec3 plane, float transmission, uint seed, float rate)
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
        return weight * ambientThrough(position, towards, ROOM_FILL_REACH);

    // Drawn last, so a solid's direction and a sheet's side are the numbers they were.
    if (randomNext(state) >= rate)
        return 0.0;

    return weight * ambientThrough(position, towards, frame.mReach) / rate;
}

/// What a surface a path ends at sends back: `pathEnd`, dimmed by one occlusion ray of its own,
/// through `shadeSurface`.
///
/// **One statement of the tail the paths share**: the far end of a water ray, and the hit the eye's
/// bounce found. A pane ends its path in `shadePane`, the same terms kept apart for its filter.
///
/// **The water's legs split the sky's source off**, because what they find is what the pixel shows:
/// under a canopy, one shadow ray a pixel speckles a reflection that the same rock seen directly
/// hands to the shadow denoiser. The bounce composes it, since the wavelet filters its whole light.
///
/// @param key the pixel's own, `pixelKey`, and `ambient` and `lamps` the `SEED_` the occlusion ray
///        and the lamp reservoir draw from with it. Two, for the reason `SEED_AMBIENT_REACHING`
///        gives.
/// @param split as `gather` takes it.
/// @param ambientRate as `ambientReaching` takes it.
SplitLight shadeAtPathEnd(
    Surface hit, uint key, uint ambient, uint lamps, uint path, bool split, float ambientRate)
{
    const float reaching = ambientReaching(
        hit.mPosition, hit.mNormal, hit.mGeometric, hit.mTransmission, key + ambient, ambientRate);

    return shadeSurface(hit, glossOf(hit), pathEnd(hit.mPosition, reaching), key, lamps, path, split);
}

/// What a see-through layer sends back, in the pieces the pane filter takes apart.
struct SeenPane
{
    /// What it glows with, which nothing drew: `litSurface` with no light arriving.
    vec3 mGlow;

    /// What a path end drew for it, per unit albedo — `pathEnd` under its one occlusion ray, and
    /// `gather`'s diffuse half — and what its lobe reflects of that, whole.
    vec3 mDiffuse;
    vec3 mSpecular;

    SurfaceResponse mResponse;
};

/// `shadeAtPathEnd` for a layer the eye looks through, with what was drawn kept apart from what was
/// not: the pane filter averages the one over time, and the glow is exact as it stands.
///
/// **The same terms, so a pane composed from these is the pane `shadeAtPathEnd` shades**: the glow
/// plus the albedo times the drawn light plus the lobe is `litSurface` of the two, to its rounding.
///
/// **At `AMBIENT_EXTERIOR_RATE`**, because the pane filter takes what it draws, as the glossy filter
/// takes what a lobe's path end draws at the same rate.
///
/// @param key,ambient,lamps as `shadeAtPathEnd` takes them.
SeenPane shadePane(Surface hit, uint key, uint ambient, uint lamps)
{
    const float reaching = ambientReaching(
        hit.mPosition, hit.mNormal, hit.mGeometric, hit.mTransmission, key + ambient, AMBIENT_EXTERIOR_RATE);
    const DirectLight lit = gather(hit, glossOf(hit), key, lamps, PATH_SEEN, false);

    return SeenPane(litSurface(hit, vec3(0.0), vec3(0.0)), pathEnd(hit.mPosition, reaching) + lit.mDiffuse,
        lit.mSpecular, responseOf(hit));
}

/// What one bounce brings back, in the two halves `shadeSolid` hands on apart.
struct Bounce
{
    /// Per unit albedo, as the direct light's diffuse half is: the composite multiplies it by the
    /// diffuse albedo.
    vec3 mDiffuse;

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

/// What a bounce brings back when it reaches nothing.
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
/// picture of the world: the deck, the sheets and the stars where they are, and no fill. With no
/// discs, which `gather` asks the lobe for already. A branch and not a factor, because the halves
/// are the draw's own split and the reflected sky is a deck's reading the diffuse half never needs.
vec3 bounceEscape(vec3 position, BounceDraw drawn, vec3 weight)
{
    if (!skyLights())
        return vec3(0.0);

    const vec3 sky = drawn.mSpecular ? reflectedSky(position, drawn.mTowards, 0.5 * drawn.mSpread, false)
                                     : skyGlow(drawn.mTowards);
    return weight * sky * daylightReaching(position);
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
vec3 bounceArriving(Surface surface, BounceDraw drawn, vec3 weight, uvec2 pixel)
{
    // **Far ground out of doors is handed the escape rather than asked whether it escaped**, which
    // is the same answer the miss below arrives at by tracing for it. `BOUNCE_REACH` says what that
    // costs and why the room is not in it.
    const vec3 fromEye = surface.mPosition - frame.mOrigin;
    if (skyLights() && surface.mGround && dot(fromEye, fromEye) > BOUNCE_REACH * BOUNCE_REACH)
        return bounceEscape(surface.mPosition, drawn, weight);

    // Drawn last, so the side, the direction and the escape are the numbers they were. One path
    // at a rate of one: no draw reaches it, and the weight is divided by one.
    uint traced = randomSeed(pixelKey(pixel) + SEED_BOUNCE_TRACED);
    if (randomNext(traced) >= frame.mBounceRate)
        return vec3(0.0);

    weight /= frame.mBounceRate;

    // **An inline query inside the closest-hit shader, and not a second launch-side trace.** The
    // one ray Shader Execution Reordering's sources point at is this one, and sorting for it was
    // measured: 20 percent slower out of doors and 30 in a room, because a bounce indoors is short
    // and lands on the same few surfaces, so there is no coherence left to recover.
    // A diffuse bounce is not drawn: it carries light, and a surface it met from behind still
    // carries it.
    const Surface hit = trace(WorldRay(surface.mPosition, drawn.mTowards), SHADOW_BIAS,
        Cone(surface.mFootprint, drawn.mSpread), solidMask(frame.mRayMask), drawn.mSpecular);

    if (!hit.mHit)
        return bounceEscape(surface.mPosition, drawn, weight);

    // **Its glow is counted here, because this is the only path it takes.** Nothing gives a glowing
    // surface a lamp of its own — `EMISSIVE_INTENSITY` says what measuring that showed — so a ray
    // that lands on a mushroom cap is what carries the cap's glow back to whatever sent it.
    //
    // **One call with the path chosen, and not one per half.** Written out twice, the whole end of
    // the path is two copies, and a warp whose lanes drew both halves runs them one after the
    // other. Chosen at run time, the diffuse half's hit asks the moons and finds they weigh nought.
    return weight
        * composed(shadeAtPathEnd(hit, pixelKey(pixel), SEED_AMBIENT_REACHING, SEED_LAMPS_BOUNCE,
            drawn.mSpecular ? PATH_SEEN : PATH_INDIRECT, false, AMBIENT_EXTERIOR_RATE));
}

/// What reaches a surface from everything that is not a light: one bounce, off the half
/// `bounceDraw` chose.
///
/// **Traced only from the hit the eye found.** A shader with no recursion cannot bounce a bounce, and
/// it should not: what the second hit gathers is `pathEnd`, the flat ambient that stands in for the
/// rest of the path. That is also what keeps `shadeSurface` from calling itself — the water's
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
        return Bounce(vec3(0.0), vec3(0.0));

    const vec3 arriving = bounceArriving(surface, drawn, drawn.mWeight * sided, pixel);

    return drawn.mSpecular ? Bounce(vec3(0.0), arriving) : Bounce(arriving, vec3(0.0));
}

/// What a solid the eye found sends back, in the channels' pieces.
struct SeenSolid
{
    /// Everything resolved but what a filter takes, the glow, in `mRest`, and what the sky's source
    /// adds and whether its rays got through: `CHANNEL_SUNLIT`'s two halves.
    SplitLight mLight;

    /// The diffuse light the wavelet filters, per unit albedo: the one bounce, and the lamps.
    vec3 mBounce;

    /// What the solid is in the filter's terms.
    SurfaceResponse mResponse;

    /// What the lobe reflects of the lamps and of the one bounce, whole, and the perceptual
    /// roughness of the lobe that reflects it, or `SPECULAR_NO_LOBE`: `CHANNEL_SPECULAR`. Not
    /// multiplied by the diffuse albedo, for the reason `Bounce::mSpecular` gives.
    vec3 mSpecular;
    float mRoughness;
};

/// What a solid the eye found is: its direct light, the one bounce it gathers, the sky's source
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
    const DirectLight lit = gather(hit, gloss, pixelKey(pixel), SEED_LAMPS_EYE, PATH_SEEN, true);
    const Bounce bounced = bounceLight(hit, gloss, pixel, cone);

    // **The lamps' diffuse half joins the bounce, and the filter takes both**, which is what a
    // shipped path tracer does with its direct lights (RTXDI's diffuse beside the indirect, under
    // ReLAX): one lamp drawn a pixel is as noisy as one bounce, and both are demodulated the same way.
    // Split, `gather`'s diffuse half holds the lamps alone, and its specular half their lobe, which
    // joins the bounce's lobe for the glossy filter.
    SeenSolid seen;
    seen.mLight = SplitLight(litSurface(hit, vec3(0.0), vec3(0.0)), skyLight(hit, lit), lit.mSkyOpen);
    seen.mBounce = bounced.mDiffuse + lit.mDiffuse;
    seen.mSpecular = lit.mSpecular + bounced.mSpecular;
    seen.mRoughness = gloss.mGlossy ? hit.mRoughness : SPECULAR_NO_LOBE;
    seen.mResponse = responseOf(hit);
    return seen;
}

#endif
