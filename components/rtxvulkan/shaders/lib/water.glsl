#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_WATER_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_WATER_GLSL

// Shading a water surface: one path a field, the reflection or the refraction, whose surface
// replaces the water's, and what the leg it crossed takes.

#include "shared/fields.h"
#include "brdf.h"
#include "camera.h"
#include "look.h"
#include "scene.h"
#include "basis.glsl"
#include "bindings.glsl"
#include "census.glsl"
#include "fog.glsl"
#include "records.glsl"
#include "sea.glsl"
#include "shading.glsl"
#include "sky.glsl"
#include "traversal.glsl"
#include "underwater.glsl"

/// How far a ray that went up and found nothing at all is taken to have travelled.
///
/// **A length for a miss and never the test for one**, which is `WaterRun::mFound`: read as the
/// test, it takes every surface further off than this for sky, and a reflection of a far shore
/// for one with no vector to reproject it by. Nothing measures water with it: a ray that went down and
/// found nothing took `WATER_UNBOUNDED_PATH` instead.
const float WATER_MAX_PATH = 2000.0;

/// How long a column of water with no bottom to it is taken to be, in world units.
///
/// **Not a distance to anything.** It is a distance past which nothing behind the water survives:
/// blue outlasts every other channel and this is thirteen of its e-foldings, so whatever stands
/// behind the column arrives at about a part in a million however bright it is. The column's own
/// scattering has settled long before that, so this is the sea's own asymptote and raising the
/// number moves no pixel.
const float WATER_UNBOUNDED_PATH = 40000.0;

/// Where the image a water path shows appears to stand along the eye's own ray: what the surface's
/// motion vector does not describe, and the distance the denoiser's guides place the replaced surface
/// at.
///
/// **A plane's images, as the place to ask how they move.** A mirror folds the path, so what the
/// reflection found appears the whole path away; a refraction shortens it, so what lies past the
/// surface appears at its distance over the index it crosses into. The waves bend both, and no
/// place describes a bent image exactly; what these say is how far it stands from the surface, and
/// so how far apart the two move.
struct WaterImage
{
    /// Nought where the ray went down and found nothing: the water's own column, which has no image.
    float mShare;
    float mAway;

    /// Whether the ray went up and found nothing: the sky, which stands at infinity and moves as the
    /// sky does.
    bool mOnSky;
};

/// What a path that went `distance` along `leg`, and `found` a surface there or not, shows, seen from
/// `before` along the eye's ray: at `folded` of the path past the surface, and at infinity where it
/// went up and found nothing.
WaterImage imageOf(bool found, float distance, WorldRay leg, float before, float folded)
{
    const bool imaged = found || leg.mAlong.z > 0.0;
    return WaterImage(imaged ? 1.0 : 0.0, before + distance * folded, !found);
}

/// Which path a water pixel's field follows: the reflection, or what is seen through the surface.
const uint WATER_MIRROR = 0u;
const uint WATER_BED = 1u;

/// What a water walk is for: the trace's shading, which takes an underwater leg's column in its
/// closed forms, or the media stage's, which marches that leg's shafts.
const uint WATER_SHADES = 0u;
const uint WATER_MARCHES = 1u;

/// The path the field of `pixel` follows this frame (`fieldOf`), which the swap turns over every
/// frame: the first field the mirror's and the second the bed's, as Quake II RTX's odd field
/// refracts.
uint waterFieldOf(uvec2 pixel)
{
    return fieldOf(pixel, fieldSwapOf(frame.mFrame)) == 0u ? WATER_MIRROR : WATER_BED;
}

/// What one field of a water pixel sees through the surface the eye met: **one path of the two**,
/// the reflection or the refraction, whose surface replaces the water's as the pixel's, as Quake II
/// RTX's `reflect_refract` replaces it (`docs/rtx/architecture.md`, "Every water pixel splits").
/// The two fields of a neighbourhood follow the two paths, and the composite blends each split
/// pixel with its four neighbours in the other field, so each path carries twice its Fresnel share
/// (`mWeight`).
///
/// **The shore's ground stands in the bed's path.** Over the last half metre of depth the surface
/// fades out (`WATER_SHORE_FADE`) and the ground the dry pixel beside it is shows through: the bed's
/// path is then bent by the water's share alone, and carries the dry share whole and the wet share
/// through the leg, so a waterline is the ground the dry pixel shows, faded into the water's.
///
/// **A walk** (`visibility.comp`, `media.comp`): the plane, the waves and the Fresnel
/// term worked out of the walk's surface when it starts, with the shore's ray; the path's ray, which
/// resolves what it found into the walk's surface; and the leg it crossed, whose column under the
/// water is its closed forms, or for the media stage a walk of its own (`ColumnRun`), as the step's
/// purpose says (`stepWater`).
struct WaterRun
{
    /// `RUN_IDLE` once the answer below is whole.
    uint mStep;

    vec3 mIncident;

    /// The pixel's own cone, which the rays cast from here open at.
    Cone mCone;

    /// `WATER_MIRROR` or `WATER_BED`.
    uint mField;

    /// What the start worked out of the surface.
    vec3 mPosition;
    float mRounding;
    float mFootprint;
    float mBefore;
    bool mFromBelow;
    vec3 mPlane;
    vec3 mNormal;
    float mLobe;
    float mFresnel;
    float mClearing;

    /// How much of the pixel is water at all, from nothing at the waterline to one over half a metre
    /// of depth.
    float mShore;

    /// The water's own surface in the filter's terms: the wave's normal, no albedo. What the pixel
    /// keeps where its path found nothing to replace it with.
    SurfaceResponse mResponse;

    /// Whether the path is the reflection, whether the two fields follow different paths — past
    /// the critical angle, under the surface, both reflect, and the pixel is not split — the path's
    /// ray, and which medium its leg crosses.
    bool mMirror;
    bool mSplit;
    WorldRay mPath;
    bool mUnderwater;
    float mPathLobe;
    RayAsk mPathAsk;

    /// What the path found and how far, the weight the light at its end arrives by — twice the
    /// path's share where the pixel splits, times what the leg let through — what the leg scattered
    /// at that share, what a miss brings back, and where the image stands.
    bool mFound;
    float mDistance;
    vec3 mWeight;
    vec3 mScattered;
    vec3 mMissed;
    WaterImage mImage;

    /// Whether the leg's column is one the sun's shafts change (`ColumnStart::mMarches`), and, where
    /// the media stage marched it, what they changed of the path's scattering.
    bool mMarches;
    vec3 mShafts;
};

const uint WATER_START = 1u;
const uint WATER_SHORE = 2u;
const uint WATER_PATH = 3u;
const uint WATER_LEG = 4u;

WaterRun waterRunOf(vec3 incident, Cone cone, uint field)
{
    WaterRun run;
    run.mStep = WATER_START;
    run.mIncident = incident;
    run.mCone = cone;
    run.mField = field;
    run.mPosition = vec3(0.0);
    run.mRounding = 0.0;
    run.mFootprint = 0.0;
    run.mBefore = 0.0;
    run.mFromBelow = false;
    run.mPlane = vec3(0.0, 0.0, 1.0);
    run.mNormal = vec3(0.0, 0.0, 1.0);
    run.mLobe = 0.0;
    run.mFresnel = 0.0;
    run.mClearing = 0.0;
    run.mShore = 1.0;
    run.mResponse = noResponse();
    run.mMirror = true;
    run.mSplit = false;
    run.mPath = WorldRay(vec3(0.0), vec3(0.0, 0.0, 1.0));
    run.mUnderwater = false;
    run.mPathLobe = 0.0;
    run.mPathAsk = noRay();
    run.mFound = false;
    run.mDistance = 0.0;
    run.mWeight = vec3(0.0);
    run.mScattered = vec3(0.0);
    run.mMissed = vec3(0.0);
    run.mImage = WaterImage(0.0, 0.0, false);
    run.mMarches = false;
    run.mShafts = vec3(0.0);
    return run;
}

/// The weights of the path's two shares of the pixel: what arrives whole, without crossing the
/// water, and what crosses the leg — the shore's dry ground and the water's own path — each twice
/// over where the pixel splits.
struct WaterShares
{
    float mWhole;
    float mThrough;
};

WaterShares waterSharesOf(WaterRun run)
{
    const float split = run.mSplit ? 2.0 : 1.0;
    const float fresnel = run.mFresnel;
    return run.mMirror ? WaterShares(0.0, split * fresnel * run.mShore)
                       : WaterShares(split * (1.0 - run.mShore), split * (1.0 - fresnel) * run.mShore);
}

/// @param surface the walk's surface: the water's own when the walk starts, and what the path found
///        once its ray is answered.
/// @param purpose `WATER_SHADES` or `WATER_MARCHES`, a constant of the kernel's call, so a kernel
///        that marches no leg holds no column's walk.
void stepWater(
    inout WaterRun run, inout ColumnRun column, Surface surface, RayAnswer answered, inout RayAsk ask, uint purpose)
{
    const vec3 incident = run.mIncident;

    if (run.mStep == WATER_START)
    {
        // **Which side of the water a ray is on is a question about the plane, not about a wave.** At
        // a glancing angle a facet can tilt far enough to face away from the ray, and reading that as
        // "the camera is underwater" sends the reflection down into the seabed and turns the far
        // water white. Water is the one surface whose sides have absolute names: it is a horizontal
        // plane, so a ray travelling upward into it came from underneath, whatever the quad's winding
        // says.
        const bool fromBelow = incident.z > 0.0;
        const vec3 plane = fromBelow ? vec3(0.0, 0.0, -1.0) : vec3(0.0, 0.0, 1.0);

        // Keyed off world position rather than anything interpolated, so one cell's surface continues
        // into the next without a seam at the boundary.
        const WaterSurface sea = waterSurfaceAt(surface.mPosition.xy, surface.mFootprint);

        // **The lost slopes as a roughness, the one a painted map states.** The cone they widen a
        // reflection by is the lobe's own, as a solid's is, and never wider than a diffuse bounce's.
        const float roughness = slopeRoughness(sea.mLostSlope);
        const float lobe = ggxConeWidth(roughness * roughness, BOUNCE_SPREAD);
        vec3 normal = fromBelow ? -sea.mNormal : sea.mNormal;

        // A facet still facing away is one the surface would have hidden behind the wave in front of
        // it, and tilting it back toward the plane is what keeps a glancing reflection finite.
        normal = facingRay(normal, plane, incident);

        // **Schlick at the angle in the air, which from below is the refracted one.** The curve is
        // written for light arriving from the rarer medium; taken at the incident angle under the
        // surface it gives 0.024 at the critical angle and then all of it past it — Snell's window
        // drawn with a hard rim. At the angle the light leaves by it climbs to one as that angle
        // reaches the horizon, which is where the critical angle puts it, and past it the cosine is
        // nought.
        const float incidence = clamp(dot(-incident, normal), 0.0, 1.0);
        const float cosine
            = fromBelow ? sqrt(max(1.0 - WATER_IOR * WATER_IOR * (1.0 - incidence * incidence), 0.0)) : incidence;
        run.mFresnel = fresnelSchlick(WATER_F0, 1.0, schlickWeight(cosine));

        // **The wave's normal and not the quad's**: what the filter tells this surface apart by. No
        // albedo, since water answers a ray with a reflection and a refraction and no Lambert term.
        countNotFinite(normal);
        run.mResponse = SurfaceResponse(packSurfaceNormal(normal), vec3(0.0), vec3(0.0), vec3(1.0));

        run.mFromBelow = fromBelow;
        run.mPlane = plane;
        run.mNormal = normal;
        run.mLobe = lobe;
        run.mPosition = surface.mPosition;
        run.mRounding = surface.mRounding;
        run.mFootprint = surface.mFootprint;

        // How far the eye's own ray had come, which a leg leaving into the air carries on from and
        // what the image stands past: the hit's own distance, and not one taken back off six-figure
        // coordinates.
        run.mBefore = surface.mDistance;

        // **How much water is under *this pixel*, which is the whole of what a shore is.** Straight
        // down rather than along anything, and worth a ray of its own: the path cast below leaves at
        // an angle, so what it measures is how far *it* travelled — which at a shore is a question
        // about the slope beyond rather than about the depth here. `WATER_SHORE_FADE` says what that
        // drew.
        //
        // From underneath there is no shore: the distance to a bed says nothing about a surface seen
        // from below it, and the ray is spared.
        //
        // **And the ray is no longer than the band it answers for.** The fade saturates at
        // `WATER_SHORE_FADE`, so a bed further down and a bed nowhere at all are the same answer —
        // which makes that length the ray's own limit, and stops every pixel of open water crossing
        // the sea to be told it is deep.
        //
        // **Measured from the surface**, though the ray leaves from under it: the depth is what the
        // ray travelled and what its start stands under the surface, straight down.
        run.mShore = 1.0;
        if (!fromBelow)
        {
            const vec3 down = vec3(0.0, 0.0, -1.0);
            const vec3 below = leaveSurface(surface.mPosition, plane * surface.mRounding, down);
            run.mClearing = surface.mPosition.z - below.z;
            ask = solidWithinAsk(WorldRay(below, down), 0.0, WATER_SHORE_FADE - run.mClearing,
                Cone(surface.mFootprint, run.mCone.mSpread));
        }
        run.mStep = WATER_SHORE;
        return;
    }

    const bool fromBelow = run.mFromBelow;
    if (run.mStep == WATER_SHORE)
    {
        if (!fromBelow)
            run.mShore = smoothstep(0.0, WATER_SHORE_FADE,
                withinOf(answered.mMet, WATER_SHORE_FADE - run.mClearing) + run.mClearing);

        // **Past the critical angle looking up from underwater the surface is a mirror** and there is
        // nothing behind it to see: both fields reflect, whole, and the pixel is not split.
        const vec3 bent = refract(incident, run.mNormal, fromBelow ? WATER_IOR : 1.0 / WATER_IOR);
        const bool whole = dot(bent, bent) < 1e-6;
        run.mSplit = !whole;
        run.mMirror = whole || run.mField == WATER_MIRROR;

        // **The reflection stays on the eye's side of the plane, and the refraction crosses it.** Each
        // leaves off the *plane*, not the facet: what a ray has to clear is the quad, and only the
        // plane's normal is guaranteed to take it off that.
        //
        // **Water that is not there cannot bend light.** The refraction is blended back toward the
        // incident by the same term that fades the surface, or the last pixel of water still shows a
        // piece of ground displaced by a fifth of a radian from the dry pixel beside it — a hard line
        // however faint the surface over it has been made.
        //
        // Refraction bends by an eighth of what reflection does from above and a sixth from below, so
        // what is seen *through* the surface is blurred correspondingly less by the same lost slopes.
        const vec3 along = run.mMirror ? reflect(incident, run.mNormal) : normalize(mix(incident, bent, run.mShore));
        run.mPath = WorldRay(leaveSurface(run.mPosition, run.mPlane * run.mRounding, along), along);
        run.mUnderwater = run.mMirror == fromBelow;
        run.mPathLobe = run.mMirror ? run.mLobe : refractedConeWidth(run.mLobe, WATER_IOR, fromBelow);

        // Drawn, because a reflection is a picture of the world and shows the faces the world shows.
        // From where it left and no further: the origin already stands off the plane (`leaveSurface`).
        // **Solid geometry only.** Culling water from its own reflection removes the self-intersection
        // the offset exists to avoid, which is what once put a ribbon of flat colour along every
        // waterline.
        run.mPathAsk = traceAsk(run.mPath, 0.0, Cone(run.mFootprint, run.mCone.mSpread + run.mPathLobe),
            solidMask(frame.mRayMask), true);
        ask = run.mPathAsk;
        run.mStep = WATER_PATH;
        return;
    }

    const WaterShares shares = waterSharesOf(run);
    if (run.mStep == WATER_PATH)
    {
        // **Which way the ray went is the whole of what a miss means, and the plane is what makes that
        // answerable.** Water has absolute sides, so below the surface there is water — whether or
        // not the bed under it is one this renderer was handed. Read as sky instead, a missed
        // refraction came back through 2000 units at half of blue and drew the edge of the loaded
        // terrain across the sea, which is a line the water never had.
        const vec3 direction = run.mPath.mAlong;
        run.mFound = surface.mHit;
        run.mDistance = surface.mHit ? surface.mDistance : (direction.z < 0.0 ? WATER_UNBOUNDED_PATH : WATER_MAX_PATH);
        run.mImage = imageOf(run.mFound, run.mDistance, run.mPath, run.mBefore,
            run.mMirror ? 1.0 : (fromBelow ? WATER_IOR : 1.0 / WATER_IOR));

        // **A reflection draws its own stars, because there is no later pass to draw them for it.**
        // What a mirror shows is composited into a surface long before the display pass, so the field
        // goes in here. The water's surface evaluated no source, so its paths see the discs whole.
        run.mMissed = !run.mFound && direction.z >= 0.0
            ? reflectedSky(run.mPath.mFrom, direction, pixelBlur(frame.mEyes.mWorld) + 0.5 * run.mPathLobe, true)
            : vec3(0.0);

        // **Water whole along a leg on the water's side, and not split at the plane as the eye's own
        // ray is** (`waterAlong`). A leg on the water's side left the underside of its surface, and a
        // facet a wave tilted can send a reflection climbing — which the surface above it would turn
        // back down, and which, sent into the air instead, drew the bright world over the water as
        // specks along the horizon.
        if (run.mUnderwater && purpose == WATER_MARCHES)
        {
            column = columnRunOf(run.mPath.mFrom, direction, run.mDistance, run.mFootprint);
            run.mStep = WATER_LEG;
            return;
        }
        if (run.mUnderwater)
        {
            const ColumnStart start = columnStartOf(run.mPath.mFrom, direction, run.mDistance);
            const WaterColumn under = closedColumnOf(start);
            run.mMarches = start.mMarches;
            run.mWeight = vec3(shares.mWhole) + shares.mThrough * under.mTransmittance;
            run.mScattered = shares.mThrough * under.mScattered;
            run.mStep = RUN_IDLE;
            return;
        }

        // How far a ray that left the water into the air crossed it: to what it found, or as far as
        // the eye's own ray carries air where it found nothing — `FOG_REACH`, which a sky ray is
        // charged over.
        const vec4 air = fogAlongLeg(run.mPath.mFrom, direction, run.mFound ? run.mDistance : FOG_REACH, run.mBefore);
        run.mWeight = vec3(shares.mWhole) + shares.mThrough * vec3(air.w);
        run.mScattered = shares.mThrough * air.xyz;
        run.mStep = RUN_IDLE;
        return;
    }

    const WaterColumn under = column.mColumn;
    run.mWeight = vec3(shares.mWhole) + shares.mThrough * under.mTransmittance;
    run.mScattered = shares.mThrough * under.mScattered;
    run.mShafts = shares.mThrough * column.mShafts;
    run.mStep = RUN_IDLE;
}

#endif
