// `#pragma once` everywhere else in this tree, and an include guard here for the reason
// `components/rtx/shaders/portable.h` gives.
#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHADING_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHADING_GLSL

// What an ordinary lit surface does with light: the direct sources it can ask about, the
// one bounce it traces for everything else, and the terms an upscaler demodulates by.

#include "scene.h"
#include "bindings.glsl"
#include "lights.glsl"
#include "random.glsl"
#include "sky.glsl"
#include "traversal.glsl"
#include "underwater.glsl"

/// One lamp held out of all the ones that could reach a point, and what it stands for.
///
/// **A reservoir is one candidate and the weight of everything it beat.** That second number is what
/// makes the estimator unbiased rather than merely cheap: the one held is divided by the chance it
/// was held, which is its own weight over the total, so a dim lamp that happens to win still speaks
/// for the whole cell.
///
/// It is a record rather than four locals because it is also the thing that gets carried — a
/// reservoir from the previous frame, or from a neighbour, combines with this one by the same rule
/// that built it (`.notes/rtx/shaders.md` §4.3).
struct Reservoir
{
    /// What the lamp held would deliver here with nothing in the way.
    vec3 mRadiance;

    /// Where it stands, for the one shadow ray this buys.
    vec3 mTowards;
    float mDistance;

    /// The held lamp's own weight, and the weight of every candidate including it.
    float mWeight;
    float mTotal;
};

/// The *direct* light arriving at a point and turning back out of it, per unit albedo.
///
/// **Sources that can be asked where they are, and nothing else.** The sun and the lamps are each a
/// known direction and a shadow ray; everything that arrives by having bounced off something is
/// `bounceLight`'s, and adding a fill here as well would count that half twice.
///
/// One shadow ray per light that could reach at all, and none for a light the surface faces away
/// from — the two tests before it are what keep a cell's worth of lamps affordable.
///
/// @param footprint how wide the cone that found this point had grown, which is the scale the
///        caustics are allowed to resolve waves at.
/// @param seed which draw sequence the lamp reservoir steps. **One per depth of the path**, because
///        a bounce shades a second surface and two reservoirs stepping one sequence would keep
///        correlated lamps at both ends of it.
vec3 gather(vec3 position, vec3 normal, float footprint, uint seed)
{
    vec3 radiance = vec3(0.0);

    // The sun, which is one direction everywhere and needs none of the machinery below: no falloff,
    // no reach, and a shadow ray that runs until it leaves the world rather than until it arrives.
    //
    // The cosine is taken against the sun's direction *in air*, which is exact for the flat bed this
    // mostly lights: refraction at a level surface moves no flux across a horizontal patch, so the
    // irradiance on one below is the irradiance above times whatever the path took. A tilted
    // underwater surface would want the refracted direction and gets this one.
    const float sunCosine = dot(normal, frame.mSunPosition);
    if (sunCosine > 0.0 && frame.mSunIrradiance != vec3(0.0)
        && !occluded(position, frame.mSunPosition, frame.mFar))
        radiance += frame.mSunIrradiance * sunThroughWater(position, footprint) * (sunCosine * INV_PI);

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
    uint state = randomSeed(seed);

    Reservoir kept;
    kept.mRadiance = vec3(0.0);
    kept.mTowards = vec3(0.0);
    kept.mDistance = 0.0;
    kept.mWeight = 0.0;
    kept.mTotal = 0.0;

    const uvec2 near = lampsReaching(position);
    for (uint i = near.x; i < near.y; ++i)
    {
        const Lamp lamp = lampAt(lights[lightIndices[i]], position);
        if (!(lamp.mReaching > 0.0))
            continue;

        const float cosine = dot(normal, lamp.mTowards);
        if (cosine <= 0.0)
            continue;

        const vec3 unshadowed = lamp.mIntensity * (cosine * lamp.mReaching * INV_PI);

        // A scalar to weigh a colour by, which is what a target function has to be. The luminance,
        // because what it decides is which lamp this pixel would most notice the loss of.
        const float weight = dot(unshadowed, LUMINANCE_WEIGHTS);
        if (!(weight > 0.0))
            continue;

        kept.mTotal += weight;

        // Hold the newcomer with probability `weight / total`, which leaves each candidate held in
        // proportion to its weight however many follow it — one-deep reservoir sampling.
        if (randomNext(state) * kept.mTotal <= weight)
        {
            kept.mRadiance = unshadowed;
            kept.mTowards = lamp.mTowards;
            kept.mDistance = lamp.mDistance;
            kept.mWeight = weight;
        }
    }

    // **The one ray.** Nothing is traced where every lamp was faced away from or out of reach, which
    // is most of the frame.
    if (kept.mWeight > 0.0 && !occluded(position, kept.mTowards, kept.mDistance - SHADOW_BIAS))
        radiance += kept.mRadiance * (kept.mTotal / kept.mWeight);

    return radiance;
}

/// What terminates a path: the cell's own ambient, dimmed by whatever water stands over the point.
///
/// **A stand-in for every bounce that is not being traced**, which is what it always was — the
/// difference is that it is now one level down rather than added on top of the one that is. A
/// surface the eye can see gathers a real hemisphere; what *that* ray lands on gets this instead,
/// and the path stops there.
///
/// It stands in for light that arrived from above, so what it loses to water is the column straight
/// over the point — `daylightReaching`'s approximation, and the same one the bounce's own escape to
/// the sky uses.
vec3 pathEnd(vec3 position)
{
    return frame.mAmbient * daylightReaching(position);
}

/// What a shading model made of a surface, in the terms a temporal upscaler demodulates by.
///
/// **Reported by whatever shaded the pixel rather than guessed after it.** Ray Reconstruction
/// separates a noisy pixel into a diffuse and a specular half using the albedos and the roughness it
/// is handed, so those three have to describe what this renderer actually did — and only the
/// function that did it knows. The frame used to answer with a constant roughness of one, a
/// permanently zero specular albedo and the *flat quad's* normal for water, which is a description
/// of a renderer nobody wrote.
struct SurfaceResponse
{
    /// The normal the shading used, which for water is the wave's and not the plane's.
    vec3 mNormal;

    /// What the diffuse half is multiplied by, and nothing else: the surface's own albedo, with
    /// none of what the path took off it between here and the eye.
    vec3 mDiffuse;

    /// What the specular half is multiplied by — the surface's reflectance at this angle.
    vec3 mSpecular;

    /// Nought for a mirror and one for Lambert.
    float mRoughness;
};

/// A pixel with no surface behind it: the sky, or a ray that reached nothing.
SurfaceResponse noResponse()
{
    return SurfaceResponse(vec3(0.0), vec3(0.0), vec3(0.0), 1.0);
}

/// What `shadeSurface` does, said in those terms. Perfectly rough and perfectly diffuse, because
/// that is exactly what a Lambert model is — and until there is a material model saying otherwise,
/// it is the true answer rather than a stand-in for one.
SurfaceResponse lambertResponse(Surface surface)
{
    return SurfaceResponse(surface.mNormal, surface.mAlbedo, vec3(0.0), 1.0);
}

/// What an ordinary lit surface sends back along the ray that found it.
///
/// @param incoming what arrives from everything that is not a light: a gathered hemisphere at the
///        hit the eye found, and `pathEnd` at the hit that hemisphere found. **One statement of what
///        a diffuse surface does with light, used at both depths** — writing it twice is how the two
///        would come to disagree.
vec3 shadeSurface(Surface surface, vec3 incoming, uint seed)
{
    // The emissive colour joins the light rather than the albedo, which is where the original engine
    // puts it: it sums the term with the diffuse and ambient light and multiplies the whole by the
    // texture (`files/shaders/compatibility/objects.frag:232`). Added past the albedo instead, a
    // mushroom cap carrying half against its stalk's nothing comes out flat white.
    //
    // The emissive *map* is the other way round, and that is the engine's doing too
    // (`objects.frag:244`): added after the multiply, so it glows through whatever the surface is
    // made of rather than being tinted by it.
    return surface.mAlbedo
        * (incoming + gather(surface.mPosition, surface.mNormal, surface.mFootprint, seed)
            + surface.mEmissiveColour * EMISSIVE_INTENSITY)
        + surface.mEmitted;
}

/// How fast a bounce ray's cone widens, against a primary ray's.
///
/// A diffuse bounce spreads over the whole hemisphere, and what the indirect term wants from a
/// texture is its *average* rather than any texel of it — so the cone is opened to about a radian,
/// which reads the coarse mips a bounce should see without collapsing every one to the top level.
const float BOUNCE_SPREAD = 1.0;

/// A direction about `normal`, drawn with probability proportional to its cosine.
///
/// **The one distribution that cancels the cosine term.** A diffuse surface weights what arrives by
/// `cos / pi` and this draws in exactly that proportion, so the estimator is the incoming radiance
/// itself with no weight left to carry — which is why a single sample is worth anything at all.
///
/// Malley's method: a disc sampled evenly, lifted onto the hemisphere. `sqrt(u.x)` is the disc's
/// radius, so the height off the surface is `sqrt(1 - u.x)` and averages two thirds — which is the
/// number a test can hold this to, and the half a uniform draw would give instead.
vec3 cosineDirection(vec3 normal, vec2 u)
{
    const float radius = sqrt(u.x);
    const float angle = TAU * u.y;

    // Any vector not parallel to the normal will do to build a basis from.
    const vec3 aside = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    const vec3 tangent = normalize(cross(aside, normal));

    return tangent * (radius * cos(angle)) + cross(normal, tangent) * (radius * sin(angle))
        + normal * sqrt(max(1.0 - u.x, 0.0));
}

/// What reaches a surface from everything that is not a light: one diffuse bounce.
///
/// **Traced only from the hit the eye found.** A shader with no recursion cannot bounce a bounce, and
/// it should not: what the second hit gathers is `pathEnd`, the flat ambient that stands in for the
/// rest of the path. That is also what keeps `shadeSurface` from calling itself — the water's
/// reflections already shade through it, and a bounce inside it would have no bottom.
///
/// A miss returns the sky, which is what makes the sky an emitter rather than a backdrop: outdoors
/// it is by far the largest source in the scene, and a surface facing it should be lit by it.
vec3 bounceLight(Surface surface, uvec2 pixel)
{
    const vec3 towards = cosineDirection(surface.mNormal, unitPair(pixel, STREAM_BOUNCE));
    const Surface hit
        = trace(surface.mPosition, towards, SHADOW_BIAS, surface.mFootprint, BOUNCE_SPREAD, MASK_SOLID);

    // The glow and not the disc: the sun is already a term of its own in `gather`, and a bounce
    // that found it in the sky would be the same light counted twice.
    //
    // Dimmed by the column of water over the point, on `daylightReaching`'s vertical approximation
    // and for its reason: this ray left for the sky and the sky is above, so what stands between
    // them is the depth. Without it a flooded floor reads brighter than the same floor seen from
    // over the surface, which is the disagreement M6 closed.
    if (!hit.mHit)
        return skyGlow(towards) * daylightReaching(surface.mPosition);

    return shadeSurface(hit, pathEnd(hit.mPosition), pixelKey(pixel) + SEED_LAMPS_BOUNCE);
}

#endif
