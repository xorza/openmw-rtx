#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_WATER_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_WATER_GLSL

// Shading a water surface: Fresnel across a reflection and a refraction, and what the column
// under it takes.

#include "brdf.h"
#include "camera.h"
#include "look.h"
#include "scene.h"
#include "basis.glsl"
#include "bindings.glsl"
#include "fog.glsl"
#include "random.glsl"
#include "records.glsl"
#include "sea.glsl"
#include "shading.glsl"
#include "sky.glsl"
#include "starfield.glsl"
#include "traversal.glsl"
#include "underwater.glsl"

/// How far a ray that went up and found nothing at all is taken to have travelled.
///
/// **A length for a miss and never the test for one**, which is `WaterPath::mFound`: read as the
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

/// How squarely a wave facet has to face the ray that found it before it is tilted back toward the
/// plane. Small: a guard against a facet turning away entirely, not a limit on the waves.
const float WATER_MIN_FACING = 0.03;

/// What a ray sent out from the water surface found.
struct WaterPath
{
    /// The light coming back along it, already shaded.
    vec3 mRadiance;

    /// How far it went to find that, or `WATER_MAX_PATH` where it found nothing.
    float mDistance;

    /// Whether it found a surface, which a distance cannot say: a surface further off than the
    /// sentinel is a surface and not sky.
    bool mFound;
};

/// What a ray sees along `ray` after leaving the water, and what it found to see it on.
///
/// **The pixel's own cone, not a bounce's.** A reflection and a refraction are specular: they carry
/// the footprint the primary ray had, where a diffuse bounce spreads over a hemisphere and wants the
/// coarse mip that goes with it. Traced at the bounce rate, a seabed a hundred units down gets a
/// hundred-unit footprint — every texture at its top mip, and every wave averaged out of the
/// caustics the same footprint governs.
///
/// The cone widens by the lobe as well as by the pixel: a reflection off water too fine to resolve
/// is blurred by the slopes that were averaged away, and what it reflects should blur with it —
/// by `ggxConeWidth` of the roughness those slopes stand for, as a solid's lobe widens its own.
///
/// **What a miss means is decided by which way the ray went**, because the water is a plane and its
/// sides have absolute names: downward is an unbounded column of water, upward is the sky. A caller
/// tells them apart by `mDistance` against the two sentinels.
///
/// **Solid geometry only.** Culling water from its own reflection removes the self-intersection the
/// ray offset exists to avoid, which is what once put a ribbon of flat colour along every waterline:
/// a refraction ray offset to the far side of the plane began under the ground wherever the bed sat
/// nearer the surface than the offset, and reported water of unbounded depth.
///
/// @param seed which draw sequence the lamp reservoir at the far end of this ray steps. The
///        reflection and the refraction take different ones, or both keep the same lamp.
/// @param ambientSeed the one the occlusion ray there draws from, for the same reason.
/// @param cone the pixel's own cone where the ray leaves, whose spread the lobe widens.
/// @param lobe how wide, across, the cone those slopes fill is — a *width*, as everything a spread
///        feeds is: `resolved` compares it against a wavelength and `coneLod` against a texel area,
///        and `mSpreadAngle` is the whole angle a pixel covers rather than half of one. The sky's
///        disc takes half of it, because a disc is named by its radius.
WaterPath waterRay(WorldRay ray, Cone cone, float lobe, uint seed, uint ambientSeed)
{
    const vec3 origin = ray.mFrom;
    const vec3 direction = ray.mAlong;

    // Drawn, because a reflection is a picture of the world and shows the faces the world shows.
    const Surface hit
        = trace(ray, SHADOW_BIAS, Cone(cone.mWidth, cone.mSpread + lobe), solidMask(frame.mRayMask), true);

    WaterPath path;
    path.mFound = hit.mHit;

    if (hit.mHit)
    {
        path.mDistance = hit.mDistance;

        path.mRadiance = shadeAtPathEnd(hit, ambientSeed, seed, PATH_SEEN);
        return path;
    }

    // **Which way the ray went is the whole of what a miss means, and the plane is what makes that
    // answerable.** Water has absolute sides, so below the surface there is water — whether or not
    // the bed under it is one this renderer was handed. Read as sky instead, a missed refraction
    // came back through 2000 units at half of blue and drew the edge of the loaded terrain across
    // the sea, which is a line the water never had.
    if (direction.z < 0.0)
    {
        path.mDistance = WATER_UNBOUNDED_PATH;
        path.mRadiance = vec3(0.0);
        return path;
    }

    path.mDistance = WATER_MAX_PATH;

    // **A reflection draws its own stars, because there is no later pass to draw them for it.**
    // What a mirror shows is composited into a surface long before the display pass, so the field
    // goes in here — behind whatever the sky's own order left in front of it, which is what `shown`
    // says and is the same rule `tone.comp` draws by.
    const float blur = pixelBlur(frame.mCamera) + 0.5 * lobe;

    path.mRadiance = reflectedSky(origin, direction, blur, true);

    return path;
}

/// How far a ray that left the water into the air crossed it: to what it found, or as far as the
/// eye's own ray carries air where it found nothing — `FOG_REACH`, which a sky ray is charged over.
float airSpan(WaterPath path)
{
    return path.mFound ? path.mDistance : FOG_REACH;
}

/// What a leg the surface sent out brings back to it: what it found, through the medium it crossed
/// to find it — the water's own column under the surface, the air over it.
///
/// **One statement for the reflection and the refraction, because which medium each crosses flips
/// with the side.** Seen from above, the refraction dives in and the reflection leaves into air;
/// from below, the reflection stays under and the refraction is the sky through Snell's window,
/// which has travelled no water at all. Attenuating the wrong one turns that window green.
///
/// @param footprint,pixel what the column's march is read at and draws from: the pixel's own, for
///        both legs, since both leave the same point.
/// @param before how far the eye's own ray had come, which a leg into the air carries on from —
///        `fogAlongLeg` says why it asks.
vec3 alongLeg(WaterPath path, WorldRay leg, bool underwater, float footprint, uvec2 pixel, float before)
{
    if (underwater)
        return throughWater(path.mRadiance, waterColumn(leg.mFrom, leg.mAlong, path.mDistance, footprint, pixel));

    return throughAir(path.mRadiance, fogAlongLeg(leg.mFrom, leg.mAlong, airSpan(path), before));
}

/// What a water surface answers with: what it sends back along the ray, what it is in the
/// filter's terms, what it reflects, and how much of the pixel is water at all.
struct WaterShading
{
    /// The water's whole answer, as if there were water all the way down.
    vec3 mRadiance;

    SurfaceResponse mResponse;

    /// How much of the pixel is water at all, from nothing at the waterline to one over half a
    /// metre of depth. **The caller mixes the ground in, and not `shadeWater`**, because what a
    /// pixel with no water under it is is the ground the dry pixel beside it is — shaded the way
    /// that pixel is shaded, with a bounce the water cannot gather. The reflection is already faded
    /// toward the incident ray by this term, so that the two halves of a mixed pixel look at one
    /// piece of ground.
    float mShore;
};

/// What the water sends back along the ray that found it, as if there were water all the way down.
///
/// @param pixel which pixel this is, for the draw key the two reservoirs below each offset by their
///        own `SEED_LAMPS_` constant — what the water reflects and what is seen through it are two
///        surfaces shaded from one hit, and two reservoirs seeded alike keep one lamp.
/// @param cone the pixel's own cone, which the three rays cast from here open at.
WaterShading shadeWater(Surface surface, vec3 incident, uvec2 pixel, Cone cone)
{
    const uint key = pixelKey(pixel);

    WaterShading shaded;

    // **Which side of the water a ray is on is a question about the plane, not about a wave.** At a
    // glancing angle a facet can tilt far enough to face away from the ray, and reading that as "the
    // camera is underwater" sends the reflection down into the seabed and turns the far water white.
    // Water is the one surface whose sides have absolute names: it is a horizontal plane, so a ray
    // travelling upward into it came from underneath, whatever the quad's winding says.
    const bool fromBelow = incident.z > 0.0;
    const vec3 plane = fromBelow ? vec3(0.0, 0.0, -1.0) : vec3(0.0, 0.0, 1.0);

    // Keyed off world position rather than anything interpolated, so one cell's surface continues
    // into the next without a seam at the boundary.
    const WaterSurface sea = waterSurfaceAt(surface.mPosition.xy, surface.mFootprint);

    // **The lost slopes as a roughness, the one a painted map states**, so the guide's channel holds
    // one quantity over water and land and the shore blends two of the same thing. The cone they
    // widen a reflection by is the lobe's own, as a solid's is, and never wider than a diffuse
    // bounce's.
    const float roughness = slopeRoughness(sea.mLostSlope);
    const float lobe = ggxConeWidth(roughness * roughness, BOUNCE_SPREAD);
    vec3 normal = fromBelow ? -sea.mNormal : sea.mNormal;

    // A facet still facing away is one the surface would have hidden behind the wave in front of it,
    // and tilting it back toward the plane is what keeps a glancing reflection finite.
    normal = facingRay(normal, plane, incident, WATER_MIN_FACING);

    // **Schlick at the angle in the air, which from below is the refracted one.** The curve is
    // written for light arriving from the rarer medium; taken at the incident angle under the
    // surface it gives 0.024 at the critical angle and then all of it past it — Snell's window drawn
    // with a hard rim. At the angle the light leaves by it climbs to one as that angle reaches the
    // horizon, which is where the critical angle puts it, and past it the cosine is nought.
    const float incidence = clamp(dot(-incident, normal), 0.0, 1.0);
    const float cosine
        = fromBelow ? sqrt(max(1.0 - WATER_IOR * WATER_IOR * (1.0 - incidence * incidence), 0.0)) : incidence;
    const float fresnel = fresnelSchlick(WATER_F0, 1.0, schlickWeight(cosine));

    // **The wave's normal and not the quad's**, and the roughness the lost slopes stand for: what the
    // filter tells this surface apart by. No diffuse albedo, since water answers a ray with a
    // reflection and a refraction and no Lambert term.
    shaded.mResponse = SurfaceResponse(normal, vec3(0.0), roughness);

    // Offset along the *plane*, not the facet: what a ray has to clear to avoid finding this surface
    // again is the quad, and only the plane's normal is guaranteed to take it off that.
    const vec3 leaving = surface.mPosition + plane * SHADOW_BIAS;

    // **How much water is under *this pixel*, which is the whole of what a shore is.** Straight
    // down rather than along anything, and worth a ray of its own: the two rays cast below both
    // leave at an angle, so what they measure is how far *they* travelled — which at a shore is a
    // question about the slope beyond rather than about the depth here. `WATER_SHORE_FADE` says
    // what that drew.
    //
    // From underneath there is no shore: the distance to a bed says nothing about a surface seen
    // from below it, and the ray is spared.
    //
    // **And the ray is no longer than the band it answers for.** The fade saturates at
    // `WATER_SHORE_FADE`, so a bed further down and a bed nowhere at all are the same answer — which
    // makes that length the ray's own limit, and stops every pixel of open water crossing the sea to
    // be told it is deep.
    shaded.mShore = 1.0;
    if (!fromBelow)
        shaded.mShore = smoothstep(0.0, WATER_SHORE_FADE,
            solidWithin(WorldRay(leaving, vec3(0.0, 0.0, -1.0)), SHADOW_BIAS, WATER_SHORE_FADE,
                Cone(surface.mFootprint, cone.mSpread)));

    // How far the eye's own ray had come, which a leg leaving into the air carries on from.
    const float before = distance(surface.mPosition, frame.mOrigin);

    // The reflection stays on the eye's side of the plane, and the refraction crosses it.
    const WorldRay mirrored = WorldRay(leaving, reflect(incident, normal));
    const WaterPath bounced = waterRay(
        mirrored, Cone(surface.mFootprint, cone.mSpread), lobe, key + SEED_LAMPS_MIRROR, key + SEED_AMBIENT_MIRROR);
    const vec3 reflected = alongLeg(bounced, mirrored, fromBelow, surface.mFootprint, pixel, before);

    const vec3 bent = refract(incident, normal, fromBelow ? WATER_IOR : 1.0 / WATER_IOR);
    if (dot(bent, bent) < 1e-6)
    {
        // Past the critical angle looking up from underwater, where the surface is a mirror and
        // there is nothing behind it to see: the reflection whole, which is the Fresnel term's own
        // answer there, and no refraction to trace.
        shaded.mRadiance = reflected;
        return shaded;
    }

    // **Water that is not there cannot bend light.** The refraction is blended back toward the
    // incident by the same term that fades the surface, or the last pixel of water still shows a
    // piece of ground displaced by a fifth of a radian from the dry pixel beside it — a hard line
    // however faint the surface over it has been made.
    const vec3 through = normalize(mix(incident, bent, shaded.mShore));

    // Refraction bends by a third of what reflection does, so what is seen *through* the surface is
    // blurred correspondingly less by the same lost slopes.
    const WorldRay across = WorldRay(leaving, through);
    const WaterPath behind = waterRay(across, Cone(surface.mFootprint, cone.mSpread), lobe * WATER_REFRACTION_BEND,
        key + SEED_LAMPS_THROUGH, key + SEED_AMBIENT_THROUGH);
    const vec3 refracted = alongLeg(behind, across, !fromBelow, surface.mFootprint, pixel, before);

    shaded.mRadiance = mix(refracted, reflected, fresnel);
    return shaded;
}

#endif
