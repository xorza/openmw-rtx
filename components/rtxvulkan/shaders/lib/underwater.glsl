#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_UNDERWATER_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_UNDERWATER_GLSL

// What a column of water does to the light crossing it.
//
// **Apart from `water.glsl`, so what lights a surface below the waterline does not take the
// water surface with it.** `shading.glsl`, `sprites.glsl` and the direct, landing and panes
// kernels ask what is left of a light after a column; only the kernels that meet the surface
// itself shade it, through `water.glsl`, which reads its column from here.

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
/// point, the slant path down from a light, and each step of a shaft. `ColumnRun`'s `gatheredAlong`
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
/// **A leg a step**, for a loop that answers its rays (`stepColumn`'s): the start asks the leg
/// under the water, or the one leg where the point is dry, and the next step the leg over it. **Both
/// legs asked at once** where nothing waits on the passage (`skyPassageFirstAsk`,
/// `skyPassageOverAsk`, `joinedPassage`).
struct SkyPassageRun
{
    /// `RUN_IDLE` once `mPassage` holds the answer.
    uint mStep;

    SkySource mSky;
    vec3 mPosition;

    /// The step off the triangle `mPosition` stands on (`stepOf`), or nought for a point in the
    /// water. The leg over the water leaves from the surface's level, which no solid ray can meet.
    vec3 mOff;

    /// `mSky`'s path to `mPosition`.
    BentPath mBent;

    vec2 mDraw;
    bool mNearest;

    /// What the leg under the water met, while the leg over it is asked.
    Passage mUnder;

    Passage mPassage;
};

const uint SKY_PASSAGE_START = 1u;
const uint SKY_PASSAGE_ALONE = 2u;
const uint SKY_PASSAGE_UNDER = 3u;
const uint SKY_PASSAGE_OVER = 4u;

SkyPassageRun skyPassageRunOf(SkySource sky, vec3 position, vec3 off, BentPath bent, vec2 draw, bool nearest)
{
    SkyPassageRun run;
    run.mStep = SKY_PASSAGE_START;
    run.mSky = sky;
    run.mPosition = position;
    run.mOff = off;
    run.mBent = bent;
    run.mDraw = draw;
    run.mNearest = nearest;
    run.mUnder = openPassage();
    run.mPassage = openPassage();
    return run;
}

/// Whether the passage crosses the water: two legs, the one under the surface asked first.
bool skyPassageBent(SkyPassageRun run)
{
    return run.mBent.mDepth > 0.0;
}

/// The passage's first leg: the one leg where the point is dry, and the leg under the water where it
/// is not. None for a picture with shadows off.
///
/// **The picture with shadows off is open to the sky under the water too**, as `skyAsk` answers over
/// it: the leg under the surface asks `lightAsk` itself, and a hull over a flooded bed shaded a map
/// tile whose dry ground beside it no roof did.
RayAsk skyPassageFirstAsk(SkyPassageRun run)
{
    if (frame.mNoSkyShadows != 0u)
        return noRay();

    if (!skyPassageBent(run))
        return skyAsk(run.mSky, run.mPosition, run.mOff, run.mDraw, run.mNearest);

    const vec3 up = -run.mBent.mTravelling;
    return lightAsk(leaveSurface(run.mPosition, run.mOff, up), up, run.mBent.mPath, run.mNearest);
}

/// The leg over the water, from where the bent path meets the surface: asked whatever the leg under
/// it met, so it is known before either is answered. None where the passage is one leg.
RayAsk skyPassageOverAsk(SkyPassageRun run)
{
    return skyPassageBent(run)
        ? skyAsk(run.mSky, vec3(run.mBent.mMet, frame.mWaterLevel), vec3(0.0), run.mDraw, run.mNearest)
        : noRay();
}

/// The passage of a bent path whose leg under the water answered `under` and whose leg over it,
/// leaving the surface `path` along the bent line, answered `over`.
///
/// The leg under the water is the nearer: what stopped it stands nearer than anything over it. A
/// stopped ray's through is one, as `Passage::mThrough` says, whichever leg stopped it.
Passage joinedPassage(Passage under, Passage over, float path)
{
    const float open = under.mOpen * over.mOpen;
    return Passage(open, open > 0.0 ? under.mThrough * over.mThrough : 1.0,
        under.mOpen < 1.0 ? under.mOccluder : min(path + over.mOccluder, SHADOW_PENUMBRA_CLEAR));
}

void stepSkyPassage(inout SkyPassageRun run, RayAnswer answered, inout RayAsk ask)
{
    if (run.mStep == SKY_PASSAGE_START)
    {
        if (frame.mNoSkyShadows != 0u)
        {
            run.mPassage = openPassage();
            run.mStep = RUN_IDLE;
            return;
        }

        ask = skyPassageFirstAsk(run);
        run.mStep = skyPassageBent(run) ? SKY_PASSAGE_UNDER : SKY_PASSAGE_ALONE;
        return;
    }

    if (run.mStep == SKY_PASSAGE_ALONE)
    {
        run.mPassage = passageAnswered(answered);
        run.mStep = RUN_IDLE;
        return;
    }

    if (run.mStep == SKY_PASSAGE_UNDER)
    {
        run.mUnder = passageAnswered(answered);
        ask = skyPassageOverAsk(run);
        run.mStep = SKY_PASSAGE_OVER;
        return;
    }

    run.mPassage = joinedPassage(run.mUnder, passageAnswered(answered), run.mBent.mPath);
    run.mStep = RUN_IDLE;
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
/// way to. A submerged surface is shadowed because `GatherRun` traces its own ray and water
/// carries a mask bit that keeps it out of occlusion — so a rock over the sea darkened the bed under
/// it and left the water in front of the bed as bright as ever. The ray goes from where the light
/// met the surface, which the march has already worked out to read the lens at, and it is the
/// surface's own question — `skyVisible`.
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
/// of one.
struct WaterColumn
{
    vec3 mTransmittance;
    vec3 mScattered;
};

/// A column's closed forms: what the water lets through, the sky's and the moons' light it scatters
/// toward the eye, the sun's beam, and whether the sun's shafts are worth a march (`ColumnRun`).
struct ColumnStart
{
    vec3 mTransmittance;
    vec3 mSkyMoons;
    vec3 mBeam;

    /// The beam's share of the column's light, which a march under `WATER_SHAFT_FLOOR` is not run
    /// for.
    float mShare;
    bool mMarches;
};

ColumnStart columnStartOf(vec3 from, vec3 direction, float path)
{
    // **The sky's closed form at the ray's own angle**, `k` one: the sky arrives from above,
    // through the column over each point. Looking straight down, `g` is two and this is the
    // `(1 - T^2) / 2` it was for every ray; level, it is `1 - T`, and looking up it gathers more.
    const vec3 transmittance = waterTransmittance(path);
    const vec3 sky
        = frame.mWaterScatter * gatheredAlong(direction, 1.0, path) * frame.mAmbient * daylightReaching(from);

    // **Every light in the sky, as a surface's `GatherRun` walks them**, so the water in front of a
    // bed a moon lights is lit by the same moon. A moon's beam is the closed form alone: the
    // shaft march is the sun's.
    vec3 moons = vec3(0.0);
    if (HAS_MOONS)
        for (uint moon = SKY_SOURCE_MASSER; moon <= SKY_SOURCE_SECUNDA; ++moon)
            moons += beamAlong(skySourceAt(moon), from, direction, path);

    // The same test `fogAlong` makes before it spends anything on shafts: an interior and a night
    // both answer no, and `mSun.mIrradiance` fades to nought across dusk rather than stepping.
    if (!sunUp())
        return ColumnStart(transmittance, sky + moons, vec3(0.0), 0.0, false);

    const vec3 beam = beamAlong(frame.mSun, from, direction, path);
    const float share = brightest(beam) / max(brightest(sky + moons + beam), 1.0e-9);
    return ColumnStart(transmittance, sky + moons, beam, share, !(share < WATER_SHAFT_FLOOR));
}

/// The column the closed forms give: what a march leaves of them where it does not run, and what the
/// trace composes, the march's change to it being the media stage's (`media.comp`).
WaterColumn closedColumnOf(ColumnStart start)
{
    return WaterColumn(start.mTransmittance, start.mSkyMoons + start.mBeam);
}

/// `WaterColumn` as a walk a step a shaft ray, for the walk's one traversal (`media.comp`,
/// `panes.comp`): the closed forms when it starts, and then a step of the march each time the sky's
/// passage (`SkyPassageRun`) answers.
struct ColumnRun
{
    /// `RUN_IDLE` once `mColumn` holds the answer.
    uint mStep;

    /// Where the stretch starts, the unit direction along it, and how long it is. All three are
    /// below the surface.
    vec3 mFrom;
    vec3 mDirection;
    float mPath;

    /// How wide the ray's cone is, which is the band limit the caustics are read at. The cone at the
    /// far end rather than one per step: the depth's own blur is the larger of the two everywhere a
    /// shaft is visible.
    float mFootprint;

    /// The closed forms, and the march's sums so far.
    vec3 mTransmittance;
    vec3 mSkyMoons;
    vec3 mBeam;
    SunUnderWater mSun;
    float mShow;
    vec3 mLit;
    vec3 mPlain;
    float mBehind;
    float mOffset;
    vec2 mAimed;
    uint mShaft;

    /// The step's weight with its lens, `weight * mix(1, caustic, show)`, which the step's
    /// visibility multiplies once its ray answers.
    vec3 mLensed;

    /// The answer, and what the march changed of the closed forms' scattering in it: the beam by the
    /// share its shafts gathered or lost, nought where no march ran.
    WaterColumn mColumn;
    vec3 mShafts;
};

const uint COLUMN_START = 1u;
const uint COLUMN_SHAFT = 2u;

ColumnRun columnRunOf(vec3 from, vec3 direction, float path, float footprint)
{
    ColumnRun run;
    run.mStep = COLUMN_START;
    run.mFrom = from;
    run.mDirection = direction;
    run.mPath = path;
    run.mFootprint = footprint;
    run.mTransmittance = vec3(0.0);
    run.mSkyMoons = vec3(0.0);
    run.mBeam = vec3(0.0);
    run.mSun = SunUnderWater(vec3(0.0), 0.0);
    run.mShow = 0.0;
    run.mLit = vec3(0.0);
    run.mPlain = vec3(0.0);
    run.mBehind = 0.0;
    run.mOffset = 0.0;
    run.mAimed = vec2(0.0);
    run.mShaft = 0u;
    run.mLensed = vec3(0.0);
    run.mColumn = WaterColumn(vec3(0.0), vec3(0.0));
    run.mShafts = vec3(0.0);
    return run;
}

/// Starts the shaft march's step `run.mShaft`: its sun ray, under the water and over it.
void startShaftStep(inout ColumnRun run, inout SkyPassageRun skyPassage)
{
    const float ahead = run.mPath * float(run.mShaft) / float(WATER_SHAFT_STEPS);
    const float along = run.mBehind + run.mOffset * (ahead - run.mBehind);

    const vec3 at = run.mFrom + run.mDirection * along;
    const BentPath bent = bentPathAt(at, run.mSun);

    const vec3 weight = waterTransmittance(bent.mPath + along) * (ahead - run.mBehind);

    // **Outside the fade, because a shadow is not fine detail.** `show` brings the *pattern* in
    // across the gate, and a rock's edge has to be there whether or not the filaments are. The
    // draw is one pair carried along the R2 steps, so each step aims its own way inside the
    // disc without a second draw a step — `SEED_WATER_SHAFT` says why not the offset's.
    //
    // **Asked the way a submerged surface asks it**, `SkyPassageRun`: up the bent line to the
    // surface as well as on from it, so a hull or a rock under the water shadows the water in front
    // of it as it shadows the bed. One short ray more a step, and only where a shaft shows.
    const vec2 draw = fract(run.mAimed + float(run.mShaft) * R2_STEPS);
    skyPassage = skyPassageRunOf(skySourceAt(SKY_SOURCE_SUN), at, vec3(0.0), bent, draw, false);

    run.mLensed = weight * mix(1.0, caustic(bent.mMet, bent.mDepth, run.mFootprint), run.mShow);
    run.mPlain += weight;
    run.mBehind = ahead;
}

/// @param pixel which pixel the stretch is seen at, which the march draws from: where in its first
///        step it starts — without that the samples land on the same shells every frame and the
///        pattern reads as a set of rings — and where in the sun's disc its shadow rays aim. One
///        pixel's legs draw alike, since they leave one point.
void stepColumn(inout ColumnRun run, inout SkyPassageRun skyPassage, uvec2 pixel)
{
    if (run.mStep == COLUMN_START)
    {
        const ColumnStart start = columnStartOf(run.mFrom, run.mDirection, run.mPath);
        if (!start.mMarches)
        {
            run.mColumn = closedColumnOf(start);
            run.mShafts = vec3(0.0);
            run.mStep = RUN_IDLE;
            return;
        }

        // **A ratio and not a radiance, which is what makes the march free of its own arithmetic.**
        // The same integrand twice — the sun's own way down, the way back to the eye, and the
        // extinction that is what scattered — once with the surface's lens at every step and once
        // without it. `WATER_SHAFT_STEPS` jittered steps are a poor quadrature of either, and an
        // excellent one of what separates them: the step count, the jitter and the exponentials all
        // cancel, and what is left multiplies the closed form above.
        //
        // So a ray that shows no pattern comes back with exactly `beam`, to the last bit, and a gate
        // has no ring to draw.
        run.mTransmittance = start.mTransmittance;
        run.mSkyMoons = start.mSkyMoons;
        run.mBeam = start.mBeam;
        run.mSun = sunUnderWater(frame.mSun.mDirection);
        run.mShow = smoothstep(WATER_SHAFT_FLOOR, WATER_SHAFT_SHOWN, start.mShare);
        run.mLit = vec3(0.0);
        run.mPlain = vec3(0.0);
        run.mBehind = 0.0;

        run.mOffset = randomAt(pixel, STREAM_WATER);
        uint aim = randomSeed(pixelKey(pixel) + SEED_WATER_SHAFT);
        run.mAimed = vec2(randomNext(aim), randomNext(aim));

        run.mShaft = 1u;
        startShaftStep(run, skyPassage);
        run.mStep = COLUMN_SHAFT;
        return;
    }

    const Passage passage = skyPassage.mPassage;
    const float visible = passage.mOpen * passage.mThrough;
    run.mLit += run.mLensed * visible;

    if (run.mShaft < WATER_SHAFT_STEPS)
    {
        ++run.mShaft;
        startShaftStep(run, skyPassage);
        return;
    }

    const vec3 ratio = run.mLit / max(run.mPlain, vec3(1.0e-20));
    run.mColumn = WaterColumn(run.mTransmittance, run.mSkyMoons + run.mBeam * ratio);
    run.mShafts = run.mBeam * (ratio - 1.0);
    run.mStep = RUN_IDLE;
}


#endif
