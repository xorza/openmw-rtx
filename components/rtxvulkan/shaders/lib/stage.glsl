#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_STAGE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_STAGE_GLSL

// What the trace's stages share: the pixel an invocation stands for, the eye's and a shore's rays,
// which a later stage casts again, and the record the trace hands on of them
// (`docs/rtx/architecture.md`, "A stage a kernel, in one fixed order").

#include "shared/fields.h"
#include "shared/traceconstants.h"
#include "camera.h"
#include "gbuffer.h"
#include "scene.h"

#include "bindings.glsl"
#include "census.glsl"
#include "frame.glsl"
#include "pixels.glsl"
#include "random.glsl"
#include "sharedexponent.glsl"
#include "traversal.glsl"

/// The pixel this invocation of a stage stands for in the workgroup `tile` of the stored layout, or
/// false where it stands for none.
///
/// **Over the stored image, a workgroup inside one field** (`fieldGroupsAcross`): the first half of
/// the groups across covers the first field's half and the rest the second's, each in whole groups,
/// so the last column and row of them run past it, and a frame of an odd width leaves a slot a row
/// that holds no pixel. A wave then runs one field's work.
bool stagePixelIn(uvec2 tile, out uvec2 pixel)
{
    const uint width = frame.mEyes.mWorld.mWidth;
    const uint halfColumns = fieldColumns(width);
    const uint halfGroups = fieldGroupsAcross(width, TRACE_WORKGROUP_WIDTH);
    const uint side = tile.x >= halfGroups ? 1u : 0u;
    const uint across = (tile.x - side * halfGroups) * TRACE_WORKGROUP_WIDTH + gl_LocalInvocationID.x;
    const uint down = tile.y * TRACE_WORKGROUP_HEIGHT + gl_LocalInvocationID.y;
    pixel = pixelOf(uvec2(side * halfColumns + across, down), fieldSwapOf(frame.mFrame), width);
    return across < halfColumns && down < frame.mEyes.mWorld.mHeight && pixel.x < width;
}

/// `stagePixelIn` the workgroup this invocation is in, for a stage dispatched over every tile.
bool stagePixelOf(out uvec2 pixel)
{
    return stagePixelIn(gl_WorkGroupID.xy, pixel);
}

/// How many tiles a list of this frame holds (`stageTileCount`).
uint stageTileCapacity()
{
    return stageTileCount(frame.mEyes.mWorld.mWidth, frame.mEyes.mWorld.mHeight);
}

/// The tile at `slot` of `list` (`STAGE_LIST_*`), which a stage dispatched over the list runs as its
/// workgroup `slot`.
uvec2 listedTile(uint list, uint slot)
{
    const uint word = stageTiles.mTiles[list * stageTileCapacity() + slot];
    return uvec2(word & 0xffffu, word >> 16u);
}

/// Appends `tile` to `list`, once a tile.
void appendTile(uint list, uvec2 tile)
{
    stageTiles.mTiles[list * stageTileCapacity() + atomicAdd(stageTiles.mDispatches[list].x, 1u)]
        = tile.x | tile.y << 16u;
}

/// Where the landing's list starts, after the tile lists.
uint landingListStart()
{
    return STAGE_LISTS * stageTileCapacity();
}

/// Makes room for `count` pixels at the end of the landing's list, and returns the first slot: one
/// atomic a workgroup. **The list counts up its own dispatch**, a workgroup a full one of pixels,
/// so the landing's stage runs over the pixels and not over the frame (`STAGE_DISPATCH_LANDING`).
uint appendLandings(uint count)
{
    const uint lanes = TRACE_WORKGROUP_LANES;
    const uint first = atomicAdd(stageTiles.mDispatches[STAGE_DISPATCH_LANDING].w, count);
    atomicAdd(stageTiles.mDispatches[STAGE_DISPATCH_LANDING].x,
        (first + count + lanes - 1u) / lanes - (first + lanes - 1u) / lanes);
    return first;
}

/// Writes `pixel` into `slot` of the landing's list (`appendLandings`).
void listLanding(uint slot, uvec2 pixel)
{
    stageTiles.mTiles[landingListStart() + slot] = pixel.x | pixel.y << 16u;
}

/// The pixel this invocation of the landing's stage stands for, or false where it stands for none:
/// the workgroup's run of the list, one pixel a lane.
bool landingPixelOf(out uvec2 pixel)
{
    const uint slot = gl_WorkGroupID.x * TRACE_WORKGROUP_LANES + gl_LocalInvocationIndex;
    const bool listed = slot < stageTiles.mDispatches[STAGE_DISPATCH_LANDING].w;
    const uint word = listed ? stageTiles.mTiles[landingListStart() + slot] : 0u;
    pixel = uvec2(word & 0xffffu, word >> 16u);
    return listed;
}

/// The eye's ray from `tmin` on, for `pixel`, through the arms' eye or the world's: the query the
/// trace casts for each layer of its peel, and a stage casts again for the solid behind it.
///
/// **Drawn, so back faces culled** — `facingFor`. Culling nothing, the peel walked through the far
/// wall of a ghost's robe as well as the near one and composited its alpha twice.
///
/// **A see-through surface met as a solid** (`MEET_AS_SOLID`), so the peel composites the layers
/// itself, which a ray that walked past them could not hand it; and a soft edge cut at the pixel's
/// draw a frame (`softEdgeDither`).
RayAsk eyeAsk(vec3 origin, vec3 direction, float tmin, uint arms, uvec2 pixel)
{
    const uint mask = arms != 0u ? frame.mRayMask & MASK_FIRST_PERSON : worldMask(frame.mRayMask);
    const Cone cone = coneAt(eyeOf(arms));
    return resolvedRayOf(SceneQuery(origin, tmin, direction, frame.mFar, facingFor(true), mask, MEET_AS_SOLID, 1u,
        cone.mWidth, cone.mSpread, 1u, pixelKey(pixel) + SEED_SOFT_EDGE));
}

/// Which solid a pixel's stages light, as the trace found it: none, the eye's own behind its layers,
/// or what the pixel's field's water path found (`WaterRun`), cast again from the water the eye's ray
/// met.
const uint STAGE_NO_SOLID = 0u;
const uint STAGE_EYE_SOLID = 1u;
const uint STAGE_WATER_PATH = 2u;

/// Where a stage record's flags word holds whether a column of water on the pixel's path is one the
/// media stage marches (`EyeRun::mMarches`).
const uint STAGE_MEDIA = 1u << 3u;

/// And whether a see-through layer stands in front of the pixel's surface, whose peel the layers'
/// stage walks again: a pixel of a tile its list holds that no layer stands in is left as it is.
const uint STAGE_LAYERED = 1u << 4u;

/// And whether the solid is resolved at the detail of a picture (`SceneQuery::mDetailed`): the eye's
/// own, and a water path's that draws.
const uint STAGE_DETAILED = 1u << 5u;

/// And, once the bounce's stage has run (`bounce.comp`), whether the lobe drew the bounce: what it
/// brings back is the lobe's, and its landing is resolved at a picture's detail and shaded as seen
/// (`BounceDraw::mSpecular`).
const uint STAGE_BOUNCE_LOBE = 1u << 6u;

/// What the trace hands its later stages of a pixel, `STAGE_RECORD`: which solid, through which eye
/// and from where along the eye's last ray, which the media stage casts again (`eyeAsk`,
/// `WaterRun`); whether the media stage marches a column of its path, whether a layer stands in
/// front, and the detail the solid is resolved at (`storedSolidOf`); and the two halves of the pane
/// mask the whole pixel's light divides — the layers' own light and how far apart they move from
/// the surface — beside the water's share, which the trace measured whole; and what the bounce's
/// stage adds for its landing's.
struct StageRecord
{
    uint mKind;
    uint mArms;
    bool mMedia;
    bool mLayered;
    bool mDetailed;
    bool mBounceLobe;
    float mTmin;
    float mPaneOwn;
    float mPaneApart;
    float mWaterMisMoved;
};

/// Four words: the kind, the eye and the flags in the first, the start's bits in the second, the
/// layers' light's in the third, and the two shares as halves in the fourth — the upscaler's masks
/// hold them in bytes.
uvec4 packStageRecord(StageRecord record)
{
    return uvec4(record.mKind | record.mArms << 2u | (record.mMedia ? STAGE_MEDIA : 0u)
            | (record.mLayered ? STAGE_LAYERED : 0u) | (record.mDetailed ? STAGE_DETAILED : 0u)
            | (record.mBounceLobe ? STAGE_BOUNCE_LOBE : 0u),
        floatBitsToUint(record.mTmin), floatBitsToUint(record.mPaneOwn),
        packHalf2x16(vec2(record.mPaneApart, record.mWaterMisMoved)));
}

StageRecord unpackStageRecord(uvec4 words)
{
    const vec2 shares = unpackHalf2x16(words.w);
    return StageRecord(words.x & 3u, (words.x >> 2u) & 1u, (words.x & STAGE_MEDIA) != 0u,
        (words.x & STAGE_LAYERED) != 0u, (words.x & STAGE_DETAILED) != 0u, (words.x & STAGE_BOUNCE_LOBE) != 0u,
        uintBitsToFloat(words.y), uintBitsToFloat(words.z), shares.x, shares.y);
}

/// Stores the solid `asked` met (`met`) at the stored place `stored`: the instance's slot, the
/// triangle and the barycentrics' bits (`STAGE_HIT`); the ray's start with its cone's width where it
/// landed (`STAGE_RAY_FROM`); and the ray's direction with the distance (`STAGE_RAY_ALONG`). **Whole
/// words**, so the stage that resolves it resolves what the trace resolved, to the bit.
void storeStageSolid(ivec2 stored, SceneAnswer met, SceneQuery asked)
{
    RTX_STORE_WORDS(stageHit, stored, uvec4(met.mInstance, met.mPrimitive, floatBitsToUint(met.mBary)))
    RTX_STORE_COUNTED(stageRayFrom, stored, vec4(asked.mFrom, footprintOf(asked, met)))
    RTX_STORE_COUNTED(stageRayAlong, stored, vec4(asked.mAlong, met.mDistance))
}

/// The hit stored at the stored place `stored` (`storeStageSolid`), resolved at the detail `detailed`
/// with no traversal.
Surface storedHitOf(ivec2 stored, bool detailed)
{
    const uvec4 hit = imageLoad(stageHit, stored);
    const vec4 from = imageLoad(stageRayFrom, stored);
    const vec4 along = imageLoad(stageRayAlong, stored);
    return resolve(
        committedHit(hit.x, hit.y, uintBitsToFloat(hit.zw), along.w, from.w), from.xyz, along.xyz, detailed);
}

/// The solid `record` names at the stored place `stored`, resolved off its stored hit as the trace
/// resolved it (`storeStageSolid`), with no traversal; no surface for a pixel whose record names
/// none.
///
/// **The hit stored and not the ray traced again**, as Falcor's visibility buffer keeps a hit for
/// the passes after it (`docs/rtx/architecture.md`, "The surface a pixel is the hit the trace
/// stored"): the eye's traversal again cost each stage as much as the whole trace kernel.
Surface storedSolidOf(StageRecord record, ivec2 stored)
{
    if (record.mKind == STAGE_NO_SOLID)
        return noSurface(frame.mOrigin);

    return storedHitOf(stored, record.mDetailed);
}

/// The bounce's landing at the stored place `stored`, which the bounce's stage stored in the solid's
/// place, resolved as the walk that traced it would have resolved it (`resolveAsked`).
Surface storedLandingOf(StageRecord record, ivec2 stored)
{
    return storedHitOf(stored, record.mBounceLobe);
}

/// What the bounce hands its landing at the stored place `stored`, `STAGE_BOUNCE`: what light along
/// it is worth, and the lobe's modulation as the word `specularModulation` unpacks
/// (`lobeModulationWord`).
void storeBounce(ivec2 stored, vec3 weight, uint modulationWord)
{
    countNotFinite(weight);
    RTX_STORE_WORDS(stageBounce, stored, uvec4(floatBitsToUint(weight), modulationWord))
}

/// What `storeBounce` stored, the modulation unpacked.
struct StoredBounce
{
    vec3 mWeight;
    vec3 mModulation;
};

StoredBounce storedBounceAt(ivec2 stored)
{
    const uvec4 words = imageLoad(stageBounce, stored);
    return StoredBounce(uintBitsToFloat(words.xyz), unpackRgb9e5(words.w));
}

/// The lift of the pixel's surface at `pixel`, by what the path let through to it: the trace's
/// halves (`STAGE_LIFT`) by its throughput.
vec3 surfaceLiftAt(ivec2 pixel, uint width, uint swap)
{
    const ivec2 stored = storedAt(pixel, width, swap);
    const uvec2 halves = imageLoad(stageLift, stored).xy;
    const vec4 through = imageLoad(stageThroughput, stored);
    return through.rgb * through.a * vec3(unpackHalf2x16(halves.x), unpackHalf2x16(halves.y).x);
}

/// The surface's lift as the pixel shows it: a split pixel's blended with its split neighbours', as
/// the composite blends its light, and in a sum as well, which the lift is not.
vec3 shownSurfaceLift(ivec2 pixel, uint width, uint swap, bool split)
{
    const vec3 own = surfaceLiftAt(pixel, width, swap);
    if (!split)
        return own;

    vec3 other = vec3(0.0);
    float taken = 0.0;
    for (int side = 0; side < 4; ++side)
    {
        const ivec2 near = acrossOf(pixel, side);
        const bool joins = !outsideOf(near, uvec2(width, frame.mEyes.mWorld.mHeight))
            && splitIn(imageLoad(motion, channelAt(CHANNEL_MOTION, near, width, swap)));
        other += joins ? surfaceLiftAt(near, width, swap) : vec3(0.0);
        taken += joins ? 1.0 : 0.0;
    }
    return taken > 0.0 ? 0.5 * own + 0.5 * (other / taken) : own;
}

#endif
