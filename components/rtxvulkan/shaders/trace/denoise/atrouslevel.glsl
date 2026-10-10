#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_TRACE_DENOISE_ATROUSLEVEL_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_TRACE_DENOISE_ATROUSLEVEL_GLSL

// One level of an edge-stopping à-trous wavelet, over the indirect channel and the share of it that
// is the fill.
//
// **The first level writes where the accumulator will look for its history**, which is SVGF's
// feedback: what a pixel carries into the next frame is the filtered light rather than the single
// sample that went into it. It costs nothing to do — the level had to write somewhere, and the
// level after it reads that image as its own input — and it takes eight bytes a pixel off the
// accumulator, which is the one compute pass in the frame that runs at the memory limit.
// `AtrousPass::record` is where the three images take their turns.
//
// **It filters light and never a surface.** The trace divided the albedo out, so what is here is
// what varies slowly across a wall — the part a blur can average without destroying anything — and
// the composite multiplies the texture back in afterwards at full sharpness. Filtering a finished
// picture instead is what makes a denoiser look like a smear.
//
// The channel also carries what the water and the air took off that light on the way to the eye,
// which varies as slowly as fog does and so averages without harm.
//
// Dammertz et al. 2010 for the cascade, and SVGF for the normal test and its exponent of 128.
//
// **The other two edge-stopping functions are not SVGF's.** Its depth term divides by the depth
// gradient along the tap, `exp(-|z(p) - z(q)| / (sigma_z * |grad z . (p - q)| + eps))`; what is
// below is a plane-distance test, which is what the denoisers written since — NRD's among them —
// settled on, and which handles a grazing floor without needing a derivative that a ray tracer has
// no rasterizer to hand it. The luminance term is SVGF's but for one thing: it weighs by the
// variance of the accumulated history, prefiltered 3x3 at the first level, and each level carries
// what averaging left of that variance to the next in the alpha beside its colour — **the centre's
// and the tap's variance together, and not the centre's alone** (`ATROUS_LUMINANCE_SIGMA` says
// why).
//
// **The alpha holds the deviation, its square root, and not the variance itself.** Each level
// shrinks the variance by its weights' squares, and every level stores it in a half
// (`ATROUS_CHANNEL`): in dim light the variance fell under the least normal half, 6.1e-5, or was
// flushed to nought, and the brightness test refused every tap in the dark. The deviation spans half
// the exponents, so a half holds it where it held none of the variance. Every level squares what
// it reads and stores the root of what it leaves.
//
// **The first level is also NRD's history fix** (`ACCUMULATE_FIX_FRAMES`): at a pixel whose mean
// holds that few frames, its taps stand `ACCUMULATE_FIX_STRIDE / (1 + n)` apart under ReLAX's flat
// kernel, with no brightness test, so the pixel takes the light of the surface around it.
// **Here and not in a pass of its own**, because the first level already reads the accumulator's
// mean and writes the history: a pass before it would copy every pixel of every frame to rebuild
// the few the eye just uncovered, and the history starts from the rebuilt mean either way. One path,
// with the step and the weights selected. **So a fixed pixel takes the fix in place of the B3 and
// not before it**, one level fewer than ReLAX's, whose fix is a pass ahead of every level: the fix's
// twenty-five taps stand four to seven pixels apart, a support of up to twenty-eight pixels the
// B3's five at step one lies inside, and the narrow levels after it filter the fixed pixel as they
// filter every other. **And its answer is the fast mean's too** (`fast`), which ReLAX's clamp makes
// of its two histories.
//
// **Only the first level is wide** (`ATROUS_WIDE`): a 5×5 B3 kernel over a 3×3 prefilter of the
// variance, and the history fix. Every level after it is ReLAX's later pass, a 3×3 Gaussian
// weighed by the centre's own variance, which the level before already averaged: a third of the
// taps, and a fourth level that reaches 16 pixels where three 5×5 levels reached 14.

#include "shared/accumulate.h"
#include "shared/atrous.h"
#include "camera.h"
#include "colour.h"
#include "gbuffer.h"
#include "shared/sets.h"
#include "shared/shadow.h"

#include "lib/census.glsl"
#include "lib/halfround.glsl"
#include "lib/hash.glsl"
#include "lib/pixels.glsl"
#include "lib/sharedexponent.glsl"
#include "lib/surfacematch.glsl"

/// Whether this module is the first level, wide, or one of the narrow levels after it.
layout(constant_id = ATROUS_SPEC_WIDE) const bool ATROUS_WIDE = true;

layout(local_size_x = ATROUS_WORKGROUP, local_size_y = ATROUS_WORKGROUP) in;

/// The one bounce, albedo divided out, as the last level left it, and in `a` the
/// deviation of its luminance: the accumulator's clamp's for the first level, and what the last
/// level's average left of it for every level after. Where a pixel's mean holds too few frames to
/// have measured its own, the square's around it (`shortHistoryVariance`).
///
/// **A sampled image and not a storage one, on every input this pass only reads.**
/// `AtrousPass::sBindings` carries the measurement. A sampled declaration states no format, which
/// is why these three name none. The targets name none either, and hold `ATROUS_CHANNEL`.
layout(set = SET_PASS, binding = ATROUS_BIND_SOURCE) uniform texture2D source;

/// Where this level puts what it made of it, rounded to the half it holds: at random at the first
/// level, which writes the history, and to the nearest at every level after it (`atrous.h`).
layout(set = SET_PASS, binding = ATROUS_BIND_FILTERED) uniform writeonly image2D filtered;

/// The shading normal's code in `r` and the distance from the eye in `g`, in world units, with the
/// eye the ray left in its sign — `CHANNEL_SURFACE`, `packSurfaceDistance`. A ray that hit nothing
/// left `SURFACE_NO_NORMAL`, which no surface can match, so the sky neither contributes nor gets
/// filtered.
layout(set = SET_PASS, binding = ATROUS_BIND_SURFACE) uniform texture2D surfaceChannel;

/// The share of `source` that is the fill, as the last level left it, and where this level puts
/// what it made of it. **At the first level its `a` holds how many frames the mean holds**, which
/// the accumulator wrote: whether the history fix rebuilds the pixel, and how far apart its taps
/// stand; the level copies it into the history the accumulator reads it from next frame.
///
/// **Taken by the weights the whole bounce's taps earn**, and by no luminance test of its own: a
/// level is linear in what it averages, so the fill's answer stays the share of the bounce's answer
/// that it was going in. What the composite takes off the bounce by the diffuse albedo and puts back
/// by the ambient one is then the same light, and a surface whose two albedos are one is shown as
/// the bounce alone would show it.
layout(set = SET_PASS, binding = ATROUS_BIND_FILL_SOURCE) uniform texture2D fillSource;
layout(set = SET_PASS, binding = ATROUS_BIND_FILL_FILTERED) uniform writeonly image2D fillFiltered;

/// The accumulator's fast means, as the clamp wrote them: where the history fix rebuilds a pixel,
/// its answer goes here too, so the next frame's fast mean starts from the rebuilt light and not
/// from one raw sample — ReLAX's clamp copies the fixed history into the slow one, so after it the
/// two hold one light (`RELAX_HistoryClamping`). Untouched everywhere else, and at every narrow
/// level.
layout(set = SET_PASS, binding = ATROUS_BIND_FAST, ACCUMULATE_FAST) uniform writeonly uimage2D fast;

layout(push_constant, scalar) uniform Push
{
    AtrousConstants level;
};

#ifdef ATROUS_COMPOSE
// The composite's channels and the filters' answers, which the last level reads to compose the frame
// where the cascade ends, and the frame it composes over — `composite.comp`'s, at bindings of this
// module's own. No format on the light, for the reasons the composite gives.
layout(set = SET_PASS, binding = ATROUS_COMPOSE_BIND_DIRECT) uniform image2D direct;
layout(set = SET_PASS, binding = ATROUS_COMPOSE_BIND_ALBEDO, GBUFFER_ALBEDO) uniform readonly image2D albedo;
layout(set = SET_PASS, binding = ATROUS_COMPOSE_BIND_AMBIENT_ALBEDO, GBUFFER_ALBEDO)
    uniform readonly image2D ambientAlbedo;
layout(set = SET_PASS, binding = ATROUS_COMPOSE_BIND_SHADOWED) uniform readonly image2D shadowed;
layout(set = SET_PASS, binding = ATROUS_COMPOSE_BIND_LAMPED) uniform readonly image2D lamped;
layout(set = SET_PASS, binding = ATROUS_COMPOSE_BIND_SHADOW, SHADOW_VISIBILITY) uniform readonly uimage2D shadow;
layout(set = SET_PASS, binding = ATROUS_COMPOSE_BIND_LAMP_SHADOW, SHADOW_VISIBILITY) uniform readonly uimage2D lampShadow;
layout(set = SET_PASS, binding = ATROUS_COMPOSE_BIND_SPECULAR) uniform readonly image2D specular;
layout(set = SET_PASS, binding = ATROUS_COMPOSE_BIND_SPECULAR_ALBEDO, GBUFFER_ALBEDO)
    uniform readonly image2D specularAlbedo;
layout(set = SET_PASS, binding = ATROUS_COMPOSE_BIND_PANE) uniform readonly image2D pane;
layout(set = SET_PASS, binding = ATROUS_COMPOSE_BIND_PANE_ALBEDO, GBUFFER_ALBEDO) uniform readonly image2D paneAlbedo;

#include "lib/resolve.glsl"
#endif

/// **The first level's taps from a tile in shared memory** (NRD's `RELAX_AtrousSmem`): every texel
/// its 5×5 taps at step one and its 3×3 prefilter read around the workgroup, loaded once with its
/// normal decoded and its point rebuilt, where each was fetched, decoded and rebuilt by every one of
/// the twenty-five pixels whose taps reach it. The values are the ones a tap computed for itself, so
/// the level's answer is the same to the bit. A pixel the history fix rebuilds steps past the tile
/// and fetches its own taps (`fetchedTap`); one narrow level's taps stand a step apart that leaves
/// nothing to share, and the tile is one texel there.
const int TILE_REACH = 2;
const uint TILE = ATROUS_WIDE ? ATROUS_WORKGROUP + 2u * uint(TILE_REACH) : 1u;

/// Each texel of the tile: the light and its deviation, the fill, the decoded normal, and the point
/// in `xyz` with one in `w` where a surface stands there in the frame and nought where none does.
shared vec4 gLight[TILE][TILE];
shared vec3 gFill[TILE][TILE];
shared vec3 gNormal[TILE][TILE];
shared vec4 gPosition[TILE][TILE];

/// The B3 spline the wavelet is built from, as its 1D row: 1, 4, 6, 4, 1 over sixteen.
///
/// **A select and not a table, and that is a rule for every fixed table in this renderer.** `glslc`
/// cannot subscript a constant composite, so it copies the whole array into function storage once
/// per subscript *expression* — which on this hardware is scratch memory. `[[unroll]]` does not
/// undo it, because the copy is emitted before any loop is unrolled: a probe over the three passes
/// that did this took sixteen such copies to fourteen and no further, where stating the value
/// instead took them to three.
float kernelAt(int offset)
{
    const int away = abs(offset);

    return away == 0 ? 6.0 / 16.0 : (away == 1 ? 4.0 / 16.0 : 1.0 / 16.0);
}

/// The narrow levels' row: ReLAX's 3×3 Gaussian (`RELAX_Atrous`), whose three sum to one.
float gaussianAt(int offset)
{
    return offset == 0 ? 0.44198 : 0.27901;
}

/// The variance at `at`, as a 3x3 Gaussian of the level's input over the surfaces around it.
///
/// **SVGF's prefilter, at the first level alone**: one pixel's estimate over a short history is
/// itself noisy, and an edge stopped by a noisy spread is a blotch. The levels after it read a
/// variance the levels before them averaged, as ReLAX's do. A tap with no surface under it carries
/// no variance of a surface, and is left out.
float varianceAround(ivec2 local)
{
    float sum = 0.0;
    float weight = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            const ivec2 tap = local + ivec2(x, y);
            if (!(gPosition[tap.y][tap.x].w > 0.0))
                continue;

            const float w = tentWeight(ivec2(x, y));
            const float deviation = gLight[tap.y][tap.x].a;
            sum += deviation * deviation * w;
            weight += w;
        }

    // The centre is a surface, so the weight is at least a quarter.
    return sum / weight;
}

/// Where the trace's ray through `pixel` ended up, off its distance as the surface channel packs it,
/// through the eye that cast it. An arm's pixel rebuilt through the world's eye stood on a plane the
/// arm is not, and the plane test turned its neighbours away.
vec3 positionAt(ivec2 pixel, float packed)
{
    return positionAlong(eyeOfPixel(packed, level.mEyes), pixel, surfaceDistance(packed));
}

/// What one tap reads: whether a surface stands there in the frame, and its normal, point, light
/// and fill where one does.
struct Tapped
{
    bool mSurface;
    vec3 mNormal;
    vec3 mPosition;
    vec4 mLight;
    vec3 mFill;
};

/// A tap read from the images, for a pixel whose taps leave the tile.
Tapped fetchedTap(ivec2 tap)
{
    if (outsideOf(tap, uvec2(level.mEyes.mWorld.mWidth, level.mEyes.mWorld.mHeight)))
        return Tapped(false, vec3(0.0), vec3(0.0), vec4(0.0), vec3(0.0));

    const vec2 there = texelFetch(surfaceChannel, tap, 0).rg;
    if (there.x == SURFACE_NO_NORMAL)
        return Tapped(false, vec3(0.0), vec3(0.0), vec4(0.0), vec3(0.0));

    return Tapped(true, unpackSurfaceNormal(there.x), positionAt(tap, there.y), texelFetch(source, tap, 0),
        texelFetch(fillSource, tap, 0).rgb);
}

Tapped tiledTap(ivec2 local)
{
    const vec4 position = gPosition[local.y][local.x];
    return Tapped(position.w > 0.0, gNormal[local.y][local.x], position.xyz, gLight[local.y][local.x],
        gFill[local.y][local.x]);
}

/// Loads the first level's tile around the workgroup at `origin`. **Every load at a pixel clamped
/// into the frame**, and one outside it marked as no surface, for the reason `accumulateclamp.comp`
/// gives: the device runs without robust image access.
void loadTile(ivec2 origin)
{
    const uint threads = ATROUS_WORKGROUP * ATROUS_WORKGROUP;
    const ivec2 last = ivec2(level.mEyes.mWorld.mWidth, level.mEyes.mWorld.mHeight) - 1;
    for (uint i = gl_LocalInvocationIndex; i < TILE * TILE; i += threads)
    {
        const uvec2 square = uvec2(i % TILE, i / TILE);
        const ivec2 pixel = origin - TILE_REACH + ivec2(square);
        const ivec2 inside = clamp(pixel, ivec2(0), last);
        const vec2 there = texelFetch(surfaceChannel, inside, 0).rg;
        const bool surface = pixel == inside && there.x != SURFACE_NO_NORMAL;
        gLight[square.y][square.x] = texelFetch(source, inside, 0);
        gFill[square.y][square.x] = texelFetch(fillSource, inside, 0).rgb;
        gNormal[square.y][square.x] = surface ? unpackSurfaceNormal(there.x) : vec3(0.0);
        gPosition[square.y][square.x] = vec4(positionAt(inside, there.y), surface ? 1.0 : 0.0);
    }
    barrier();
}

void main()
{
    const ivec2 at = ivec2(gl_GlobalInvocationID.xy);

    // Every thread of the workgroup loads its share of the tile, the ones past the frame included.
    if (ATROUS_WIDE)
        loadTile(ivec2(gl_WorkGroupID.xy * ATROUS_WORKGROUP));

    if (outsideOf(uvec2(at), uvec2(level.mEyes.mWorld.mWidth, level.mEyes.mWorld.mHeight)))
        return;

    const vec2 seen = texelFetch(surfaceChannel, at, 0).rg;

    // A pixel with no surface under it has nothing to filter and nothing to say to its neighbours.
    if (seen.x == SURFACE_NO_NORMAL)
    {
#ifdef ATROUS_COMPOSE
        RTX_STORE_COUNTED(direct, at,
            vec4(resolvedLight(at, imageLoad(direct, at).rgb, texelFetch(source, at, 0).rgb,
                     texelFetch(fillSource, at, 0).rgb, level.mShadowed, level.mLobed),
                1.0))
#else
        RTX_STORE_COUNTED(filtered, at, texelFetch(source, at, 0))
        RTX_STORE_COUNTED(fillFiltered, at, texelFetch(fillSource, at, 0))
#endif
        return;
    }

    // The first level's centre as its tile holds it, decoded and rebuilt once.
    const ivec2 local = ivec2(gl_LocalInvocationID.xy) + TILE_REACH;
    const vec3 normal = ATROUS_WIDE ? gNormal[local.y][local.x] : unpackSurfaceNormal(seen.x);
    const float away = surfaceDistance(seen.y);
    const vec3 position = ATROUS_WIDE ? gPosition[local.y][local.x].xyz : positionAt(at, seen.y);

    // What one pixel covers where this surface is, which is the scale every plane offset below is
    // measured against. Reading `mSpreadAngle` for it instead lands on nought for a parallel
    // projection, and a footprint of nought rejects every tap but the centre — a map tile running
    // every level of this and coming out exactly as it went in.
    const float footprint = footprintAlong(eyeOfPixel(seen.y, level.mEyes), away);

    // Whether this level rebuilds the pixel's history, and how far apart its taps stand. A pixel
    // with a surface holds one frame at least.
    const vec4 fillCentre = texelFetch(fillSource, at, 0);
    const float frames = ATROUS_WIDE ? fillCentre.a : 0.0;
    const float held = level.mFixFrames > 0.0 ? frames : 0.0;
    const bool fixing = held > 0.0 && held <= level.mFixFrames;
    // The first level steps one pixel, which its tile holds the taps of; a narrow level its own.
    const int step = fixing ? int(floor(ACCUMULATE_FIX_STRIDE / (1.0 + held) + 0.5))
                            : (ATROUS_WIDE ? 1 : int(level.mStep));
    const int reach = ATROUS_WIDE ? TILE_REACH : 1;

    // **The outer taps off their lattice past `ATROUS_JITTER_STEP`**, ReLAX's offset: by a hash of
    // the pixel and the frame, so `repeat` draws it again, and truncated as ReLAX truncates it. Not in
    // the history fix, whose pass in ReLAX has no offset; a select, since a level's step is one step
    // for every lane but a fixed one.
    uint taps = seededKey(pixelKey(uvec2(at)) + SEED_WAVELET_TAPS, level.mFrame);
    const vec2 drawn = vec2(randomNext(taps), randomNext(taps));
    const bool jitters = !fixing && uint(step) > ATROUS_JITTER_STEP;
    const ivec2 offset = jitters ? ivec2(float(step) * 0.5 * (drawn - 0.5)) : ivec2(0);

    // **How noisy this pixel still is, which is what makes the brightness test mean anything.** A
    // difference of one against a variance of four is nothing; the same difference against a settled
    // pixel is an edge. The epsilon is a divide guard and nothing else. The history fix has no
    // brightness test, and pays the prefilter's nine loads all the same: skipping them for a fixed
    // pixel moved the filter zone by nothing on the default suite, three alternated rounds. A narrow
    // level reads the centre's, which the levels before it averaged. Each tap adds its own beside it.
    const vec4 centre = ATROUS_WIDE ? gLight[local.y][local.x] : texelFetch(source, at, 0);
    const float noise = ATROUS_WIDE ? varianceAround(local) : centre.a * centre.a;
    const float here = dot(centre.rgb, LUMINANCE_WEIGHTS);

    // What the average leaves of the variance, for the next level: each tap's own, by the square of
    // its weight, over the square of the whole — the variance of a weighted mean of independent
    // samples.
    vec3 sum = vec3(0.0);
    vec3 sumFill = vec3(0.0);
    float weight = 0.0;
    float variance = 0.0;

    for (int y = -reach; y <= reach; ++y)
        for (int x = -reach; x <= reach; ++x)
        {
            const ivec2 tap = at + ivec2(x, y) * step + (x == 0 && y == 0 ? ivec2(0) : offset);
            // A branch only the history fix's pixels take, whose taps leave the tile: the measurement
            // `AtrousPass` records is of the level with it.
            const Tapped tapped = ATROUS_WIDE && !fixing ? tiledTap(local + ivec2(x, y)) : fetchedTap(tap);
            if (!tapped.mSurface)
                continue;

            // Two surfaces are the same surface where they face the same way and lie in the same
            // plane. Both tests are needed: a facing test alone joins two parallel walls a room
            // apart, and a plane test alone joins a floor to the wall standing on it.
            const FacingWeights faced = facingWeights(normal, tapped.mNormal);
            const float facing = fixing ? faced.mFix : faced.mFilter;
            const float coplanar = coplanarWeight(normal, position, tapped.mPosition, footprint);

            // The third of SVGF's three, and the only one about the light rather than the surface.
            // **None in the history fix**, whose centre holds too few frames to have a spread.
            const vec4 light = tapped.mLight;
            const float tapVariance = light.a * light.a;
            const float spread = ATROUS_LUMINANCE_SIGMA * sqrt(0.5 * (noise + tapVariance)) + 1e-4;
            const float lit = fixing ? 1.0 : exp(-abs(here - dot(light.rgb, LUMINANCE_WEIGHTS)) / spread);
            const float kernel
                = fixing ? 1.0 : (ATROUS_WIDE ? kernelAt(x) * kernelAt(y) : gaussianAt(x) * gaussianAt(y));

            const float w = kernel * facing * coplanar * lit;
            sum += light.rgb * w;
            sumFill += tapped.mFill * w;
            weight += w;
            variance += tapVariance * w * w;
        }

    // The centre always passes its own tests, so the weight can only be zero if it underflowed.
    vec4 answer = weight > 0.0 ? vec4(sum / weight, sqrt(variance) / weight) : centre;
    vec4 answerFill = vec4(weight > 0.0 ? sumFill / weight : fillCentre.rgb, frames);
    uint rounding = seededKey(pixelKey(uvec2(at)) + SEED_BOUNCE_HISTORY_ROUNDING, level.mFrame);
    if (ATROUS_WIDE)
        roundedToHalves(answer, answerFill, rounding);
#ifdef ATROUS_COMPOSE
    // **Composed from the halves a stored level would hold**, so the frame is the one the composite
    // made of them: the same light to the bit, and no level written for a pass to read back.
    RTX_STORE_COUNTED(direct, at,
        vec4(resolvedLight(at, imageLoad(direct, at).rgb, nearestHalf(answer).rgb, nearestHalf(answerFill).rgb,
                 level.mShadowed, level.mLobed),
            1.0))
#else
    RTX_STORE_COUNTED(filtered, at, ATROUS_WIDE ? answer : nearestHalf(answer))
    RTX_STORE_COUNTED(fillFiltered, at, ATROUS_WIDE ? answerFill : nearestHalf(answerFill))
#endif
    if (fixing)
        RTX_STORE_WORDS(fast, at, uvec4(packRgb9e5(answer.rgb), packRgb9e5(answerFill.rgb), 0u, 0u))
}

#endif
