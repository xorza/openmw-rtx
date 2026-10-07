#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_UNDERWATER_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_UNDERWATER_GLSL

// What a column of water does to the light crossing it.
//
// **Apart from `water.glsl` because the dependency runs both ways otherwise.** A surface
// below the waterline is lit through this, so `gather` needs it — and `shadeWater` needs
// `gather`. The half that answers "what is left of this light" has no opinion about
// shading and comes first; the half that shades a water surface comes after.

#include "colour.h"
#include "gbuffer.h"
#include "look.h"
#include "shared/medium.h"
#include "scene.h"
#include "bindings.glsl"
#include "frame.glsl"
#include "lights.glsl"
#include "random.glsl"
#include "sea.glsl"
#include "traversal.glsl"

/// What a path of water this long leaves of the light crossing it, per channel.
///
/// Beer-Lambert over a length, which is the form every transmittance below takes: the column over a
/// point, the slant path down from a light, and each step of a shaft. `waterColumn`'s own `gathered`
/// is the *integral* of this along a ray rather than a value of it, which is why that one is written
/// out and why it takes a signed length.
vec3 waterTransmittance(float path)
{
    return exp(-WATER_EXTINCTION * path);
}

/// What is left of the daylight by the time it reaches a point, as a fraction per channel.
///
/// The sun and the sky both come from above, so what they lose is the water between the surface and
/// the point they land on. **This was the half that was missing**: absorbing on the way up while
/// lighting the bottom as though the water were not there makes the same column of water read
/// differently from above and below, which is what the invariant test measures.
///
/// White above the surface, and for a cell with no water at all: `waterOver` is nought there and
/// `exp(0)` is white, so no test stands in front of the arithmetic.
vec3 daylightReaching(vec3 position)
{
    return waterTransmittance(waterOver(position));
}

/// Which way the sun travels once it is under the surface, and how far it goes to reach a depth.
///
/// **Refracted at a *flat* surface**, because what the waves do to the direction averages out over
/// a path and what they do to its distribution is the caustic. A low sun is bent hard toward the
/// vertical — Snell's window seen from the light's side — so even a sun on the horizon reaches a
/// point through a path about a third longer than the depth, and never through the infinite one a
/// grazing ray in air would take.
struct SunUnderWater
{
    /// Unit, and pointing down: the way the light goes, not the way the sun lies.
    vec3 mTravelling;

    /// Path length per unit of depth, which is one for a sun overhead.
    float mSlant;
};

SunUnderWater sunUnderWater(vec3 toward)
{
    const vec3 travelling = refract(-toward, vec3(0.0, 0.0, 1.0), 1.0 / WATER_IOR);

    // Light that enters from the air meets no critical angle, so `refract` always answers, and a
    // source anywhere above the horizon travels at most 48.6 degrees off the vertical under the
    // surface, a cosine of 0.66. The floor of 0.05 — 2.9 degrees above the horizontal — binds only
    // for a source under the horizon, a moon that has set, and holds its slant at twenty.
    return SunUnderWater(travelling, 1.0 / max(-travelling.z, 0.05));
}

/// A light's path to a point under the water, bent at the surface: how deep the point is, how far
/// the light travelled under the surface to reach it, and where it met the surface — up-sun of the
/// point, by that path along the way it travels. Worked out once a point and read by both of what
/// the water does to the light there, the absorption and the lens, and by the shadow rays along it.
/// Not to be read where the point is not under the water, which `mDepth` says.
struct BentPath
{
    float mDepth;
    vec3 mTravelling;
    float mPath;
    vec2 mMet;
};

BentPath bentPathAt(vec3 position, SunUnderWater bent)
{
    const float depth = waterOver(position);
    const float path = depth * bent.mSlant;
    return BentPath(depth, bent.mTravelling, path, position.xy - bent.mTravelling.xy * path);
}

/// What a light in the sky has left, and how it has been gathered, by the time it reaches a point.
///
/// Three things happen to it on the way down. The surface lets `1 - F` of it in
/// (`waterCrossingOf`), and reflects the rest. The water absorbs along the path — the *slant* path,
/// which is longer than the depth for any source that is not overhead, and is why a bed is
/// legitimately darker seen from under the water than from above it. And the surface is a lens,
/// which is `caustic`. The shadow ray already passes the surface — water carries a mask bit that
/// keeps it out of occlusion — so this is the whole of what the water does to a light above it.
///
/// **The direction is asked for rather than read off the sun**, because a moon is above the water
/// too and stands somewhere else: a night lit through the sun's slant path is a night lit through a
/// source below the horizon.
///
/// White above the surface, and for a cell with no water at all.
///
/// @param bent the light's path to the point.
/// @param into what the surface lets in of the light, `WaterCrossing::mInto`.
vec3 lightThroughWater(BentPath bent, float footprint, float into)
{
    if (!(bent.mDepth > 0.0))
        return vec3(1.0);

    // **Read for the lens where the light met the surface, which is up-sun of where it landed**:
    // the point whose curvature focused it — and the whole of what makes a caustic move with the
    // depth and with the light rather than sitting still under the bed.
    return waterTransmittance(bent.mPath) * (caustic(bent.mMet, bent.mDepth, footprint) * into);
}

/// What the world leaves of a light in the sky at a point, asked along the path the light took:
/// under the water, bent at the surface — up the refracted line to where it met the surface, and on
/// from there along its own direction in the air — and over it, `skyVisible` itself.
///
/// **The shadow is where the light is.** `lightThroughWater` charges the bent path and reads the
/// caustic where it met the surface; cast straight along the direction in the air, a roof over the
/// water shaded a bed up-sun of where it stands, by half as much again as the depth under a sun
/// thirty degrees high. **Two rays under the water**, because what stands in the water between the
/// point and the surface is an occluder the ray in the air cannot meet. A branch on the depth and not
/// a factor, because a sunlit bed and the ground beside the water are regions of the picture and not
/// neighbouring lanes, and a ray of no length is a traversal all the same.
///
/// **In `Passage`'s two halves**: stopped where either ray was, and what both let through otherwise.
///
/// @param step the step off the triangle `position` stands on (`stepOf`), or nought for a point in
///        the water. The leg over the water leaves from the surface's level, which no
///        solid ray can meet.
/// @param bent `sky`'s path to `position`.
Passage skyPassageThrough(SkySource sky, vec3 position, vec3 step, BentPath bent, vec2 draw, bool nearest)
{
    // **The picture with shadows off is open to the sky under the water too**, as `skyPassage`
    // answers over it: the leg under the surface asks `lightPassage` itself, and a hull over a
    // flooded bed shaded a map tile whose dry ground beside it no roof did.
    if (frame.mNoSkyShadows != 0u)
        return Passage(1.0, 1.0, SHADOW_PENUMBRA_CLEAR);

    if (!(bent.mDepth > 0.0))
        return skyPassage(sky, position, step, draw, nearest);

    const vec3 up = -bent.mTravelling;
    const Passage under = lightPassage(leaveSurface(position, step, up), up, bent.mPath, nearest);
    const Passage over = skyPassage(sky, vec3(bent.mMet, frame.mWaterLevel), vec3(0.0), draw, nearest);
    // The leg under the water is the nearer: what stopped it stands nearer than anything over it. A
    // stopped ray's through is one, as `Passage::mThrough` says, whichever leg stopped it.
    const float open = under.mOpen * over.mOpen;
    return Passage(open, open > 0.0 ? under.mThrough * over.mThrough : 1.0,
        under.mOpen < 1.0 ? under.mOccluder : min(bent.mPath + over.mOccluder, SHADOW_PENUMBRA_CLEAR));
}

/// What one light in the sky sends toward the eye along the stretch, closed form: its irradiance
/// across its own line once the surface has bent it (`WaterCrossing::mBeam`), what the water
/// over the stretch's start leaves of it, the phase at the bent line's angle to the ray, and what the
/// stretch gathers.
vec3 beamAlong(SkySource source, vec3 from, vec3 direction, float path)
{
    const SunUnderWater bent = sunUnderWater(source.mDirection);
    const vec3 toward = source.mIrradiance * waterCrossingOf(source.mDirection).mBeam
        * henyeyGreenstein(WATER_ASYMMETRY, -dot(direction, bent.mTravelling));
    return frame.mWaterScatter * toward * waterTransmittance(bent.mSlant * waterOver(from))
        * gatheredAlong(direction, bent.mSlant, path);
}

/// What a stretch of water sends toward whoever is looking down it.
///
/// **The sky's half is integrated, and the sun's is too everywhere a shaft would not show.** Water is
/// one density everywhere, so a stretch of it has a closed form where the air — which thins with
/// height and drifts — has only a march. That is what lets the same arithmetic be afforded on the
/// eye's own ray, on a reflection and on a refraction alike. Only inside a narrow cone about the
/// sun's own line is anything marched, and there it is because a shaft has structure a closed form
/// cannot hold.
///
/// **The sky, arriving from every direction at once.** A phase function integrates to one over the
/// sphere, so an even sky needs none of it and the whole of what reaches a point scatters. Light
/// that scatters toward the eye had to get down there first: attenuating only the way back — `1 - T`
/// — lets deep water settle at the scattering colour at full sky brightness, which is the milky
/// sheet a real channel is not. Integrating both legs, the sky's way down being the column over
/// each point, turns that into the closed form below with `k` one: `(1 - T^2) / 2` looking straight
/// down, half as bright where it settles and markedly less red, because squaring the transmittance
/// costs red twice over.
///
/// **Each light in the sky, arriving along one line, the moons with the sun, and this is the closed
/// form.** The surface lets in `WaterCrossing::mBeam` of it. At a point `t` along the ray the light
/// has crossed `k h(t)` of water to arrive and the scattered light crosses `t` to leave, with
/// `h(t) = h - t d.z` the depth there. Both are exponentials in `t`, so their product is one:
///
///     exp(-o k h) * exp(-o (1 - k d.z) t)
///
/// and the integral over the stretch is `exp(-o k h) (1 - exp(-o g L)) / g` with `g = 1 - k d.z`.
/// **`g` is negative looking up toward the light**, where a step further along the ray is nearer the
/// surface and better lit — and the product stays bounded anyway, because a ray under the water
/// stops at the surface and `h(L)` never goes below nought.
///
/// **And the sun's half is marched where a shaft would be seen, because a shaft is a caustic.** A
/// beam of sunlight in water is the surface's own lens pattern carried along the ray: the closed
/// form gives the beam's *body* and says nothing about its structure, and the structure is the whole
/// of what makes it read as light through water rather than as haze. Every step takes the same
/// `caustic` a submerged surface takes, at its own depth and its own point of entry — which is why
/// the pattern leans down-sun as it descends instead of standing as a column.
///
/// **And it asks whether the sun reaches that point of entry at all**, which the closed form has no
/// way to. A submerged surface is shadowed because `gather` traces its own ray and water
/// carries a mask bit that keeps it out of occlusion — so a rock over the sea darkened the bed under
/// it and left the water in front of the bed as bright as ever. The ray goes from where the light
/// met the surface, which the march has already worked out to read the lens at, and it is the
/// surface's own question — `skyVisible`, with the cloud deck in it.
///
/// **Only where the beam is a real share of what the stretch sends**, which is `WATER_SHAFT_FLOOR`.
/// Everywhere else the closed form is the whole answer and nothing is marched.
///
/// **And the water over the stretch, which is no part of the stretch.** The closed forms count what
/// the stretch itself crosses and says nothing about what stands above where it begins. From above
/// there is nothing there — the stretch begins at the surface. From below it is the whole column
/// over the camera, and leaving it out let a sea a thousand units down scatter as brightly as one
/// just under the surface. `daylightReaching` is what a submerged *surface* is already dimmed by,
/// so this is the volume agreeing with the surfaces standing in it, and it is the same factor the
/// sun's half below has carried all along.
///
/// **Kept apart rather than applied**, for the reason `fogAlong` gives: the two halves separate
/// later, because the filter demodulates the bounce by the albedo and what a path took is not part
/// of one. A caller that wants the single number has `throughWater`.
struct WaterColumn
{
    vec3 mTransmittance;
    vec3 mScattered;
};

/// @param from where the stretch starts, `direction` the unit direction along it, and `path` how
///        long it is. All three are below the surface.
/// @param footprint how wide the ray's cone is, which is the band limit the caustics are read at.
///        The cone at the far end rather than one per step: the depth's own blur is the larger of
///        the two everywhere a shaft is visible.
/// @param pixel which pixel the stretch is seen at, which the march draws from: where in its first
///        step it starts — without that the samples land on the same shells every frame and the
///        pattern reads as a set of rings — and where in the sun's disc its shadow rays aim. One
///        pixel's legs draw alike, since they leave one point.
WaterColumn waterColumn(vec3 from, vec3 direction, float path, float footprint, uvec2 pixel)
{
    // **The sky's closed form at the ray's own angle**, `k` one: the sky arrives from above,
    // through the column over each point. Looking straight down, `g` is two and this is the
    // `(1 - T^2) / 2` it was for every ray; level, it is `1 - T`, and looking up it gathers more.
    const vec3 transmittance = waterTransmittance(path);
    const vec3 sky = frame.mWaterScatter * gatheredAlong(direction, 1.0, path) * frame.mAmbient
        * daylightReaching(from);

    // **Every light in the sky, as a surface's `gather` walks them**, so the water in front of a bed
    // a moon lights is lit by the same moon. A moon's beam is the closed form alone: the shaft march
    // below is the sun's.
    vec3 moons = vec3(0.0);
    if (HAS_MOONS)
        for (uint moon = SKY_SOURCE_MASSER; moon <= SKY_SOURCE_SECUNDA; ++moon)
            moons += beamAlong(skySourceAt(moon), from, direction, path);

    // The same test `fogAlong` makes before it spends anything on shafts: an interior and a night
    // both answer no, and `mSun.mIrradiance` fades to nought across dusk rather than stepping.
    if (!sunUp())
        return WaterColumn(transmittance, sky + moons);

    const SunUnderWater sun = sunUnderWater(frame.mSun.mDirection);
    const vec3 beam = beamAlong(frame.mSun, from, direction, path);

    const float share = brightest(beam) / max(brightest(sky + moons + beam), 1.0e-9);
    if (share < WATER_SHAFT_FLOOR)
        return WaterColumn(transmittance, sky + moons + beam);

    const float show = smoothstep(WATER_SHAFT_FLOOR, WATER_SHAFT_SHOWN, share);

    // **A ratio and not a radiance, which is what makes the march free of its own arithmetic.** The
    // same integrand twice — the sun's own way down, the way back to the eye, and the extinction
    // that is what scattered — once with the surface's lens at every step and once without it.
    // `WATER_SHAFT_STEPS` jittered steps are a poor quadrature of either, and an excellent one of
    // what separates them: the step count, the jitter and the exponentials all cancel, and what is
    // left multiplies the closed form above.
    //
    // So a ray that shows no pattern comes back with exactly `beam`, to the last bit, and a gate
    // has no ring to draw.
    vec3 lit = vec3(0.0);
    vec3 plain = vec3(0.0);
    float behind = 0.0;

    const float offset = randomAt(pixel, STREAM_WATER);
    uint aim = randomSeed(pixelKey(pixel) + SEED_WATER_SHAFT);
    const vec2 aimed = vec2(randomNext(aim), randomNext(aim));

    for (uint step = 1u; step <= WATER_SHAFT_STEPS; ++step)
    {
        const float ahead = path * float(step) / float(WATER_SHAFT_STEPS);
        const float along = behind + offset * (ahead - behind);

        const vec3 at = from + direction * along;
        const BentPath bent = bentPathAt(at, sun);

        const vec3 weight = waterTransmittance(bent.mPath + along) * (ahead - behind);

        // **Outside the fade, because a shadow is not fine detail.** `show` brings the *pattern* in
        // across the gate, and a rock's edge has to be there whether or not the filaments are. The
        // draw is one pair carried along the R2 steps, so each step aims its own way inside the
        // disc without a second draw a step — `SEED_WATER_SHAFT` says why not the offset's.
        //
        // **Asked the way a submerged surface asks it**, `skyPassageThrough`: up the bent line to
        // the surface as well as on from it, so a hull or a rock under the water shadows the water
        // in front of it as it shadows the bed. One short ray more a step, and only where a shaft
        // shows.
        const vec2 draw = fract(aimed + float(step) * R2_STEPS);
        const Passage passage = skyPassageThrough(skySourceAt(SKY_SOURCE_SUN), at, vec3(0.0), bent, draw, false);
        const float visible = passage.mOpen * passage.mThrough;

        lit += weight * mix(1.0, caustic(bent.mMet, bent.mDepth, footprint), show) * visible;
        plain += weight;
        behind = ahead;
    }

    return WaterColumn(transmittance, sky + moons + beam * (lit / max(plain, vec3(1.0e-20))));
}

/// What is left of `radiance` after a column of water, plus what that column sent back.
vec3 throughWater(vec3 radiance, WaterColumn column)
{
    return radiance * column.mTransmittance + column.mScattered;
}

#endif
