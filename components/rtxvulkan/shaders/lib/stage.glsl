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
#include "hash.glsl"
#include "sharedexponent.glsl"
#include "stagerecord.glsl"
#include "traversal.glsl"

/// The pixel this invocation of a stage stands for in the workgroup `tile` of the stored layout, or false
/// where it stands for none.
///
/// **Over the stored image, a workgroup inside one field** (`fieldGroupsAcross`): the first half of the
/// groups across covers the first field's half and the rest the second's, each in whole groups, so the last
/// column and row of them run past it, and a frame of an odd width leaves a slot a row that holds no pixel.
bool stagePixelIn(uvec2 tile, out uvec2 pixel)
{
    const uint width = frame.mEyes.mWorld.mWidth;
    const uint halfColumns = fieldColumns(width);
    const uint halfGroups = fieldGroupsAcross(width, TRACE_WORKGROUP_WIDTH);
    const uint side = tile.x >= halfGroups ? 1u : 0u;
    const uvec2 local
        = uvec2(gl_LocalInvocationIndex % TRACE_WORKGROUP_WIDTH, gl_LocalInvocationIndex / TRACE_WORKGROUP_WIDTH);
    const uint across = (tile.x - side * halfGroups) * TRACE_WORKGROUP_WIDTH + local.x;
    const uint down = tile.y * TRACE_WORKGROUP_HEIGHT + local.y;
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

/// Grows `dispatch`'s workgroups from `before` by `added`, in rows of `STAGE_DISPATCH_ROW`: as wide as a
/// row or the workgroups, whichever is fewer, and a row down each row they start.
void growDispatch(uint dispatch, uint before, uint added)
{
    const uint after = before + added;
    atomicMax(stageTiles.mDispatches[dispatch].x, min(after, STAGE_DISPATCH_ROW));
    atomicAdd(stageTiles.mDispatches[dispatch].y,
        (after + STAGE_DISPATCH_ROW - 1u) / STAGE_DISPATCH_ROW - (before + STAGE_DISPATCH_ROW - 1u) / STAGE_DISPATCH_ROW);
}

/// Which workgroup of a dispatch over a list this is, counted along its rows (`growDispatch`).
uint listedGroup()
{
    return gl_WorkGroupID.y * STAGE_DISPATCH_ROW + gl_WorkGroupID.x;
}

/// The tile of `list` (`STAGE_LIST_*`) this workgroup of a stage dispatched over the list runs, or
/// false past the list's end, where the last row runs out.
bool listedTileOf(uint list, out uvec2 tile)
{
    const uint slot = listedGroup();
    const bool listed = slot < stageTiles.mDispatches[list].w;
    const uint word = listed ? stageTiles.mTiles[list * stageTileCapacity() + slot] : 0u;
    tile = uvec2(word & 0xffffu, word >> 16u);
    return listed;
}

/// Appends `tile` to `list`, once a tile.
void appendTile(uint list, uvec2 tile)
{
    const uint slot = atomicAdd(stageTiles.mDispatches[list].w, 1u);
    stageTiles.mTiles[list * stageTileCapacity() + slot] = tile.x | tile.y << 16u;
    growDispatch(list, slot, 1u);
}

/// How many pixels each of the landing's arrays holds room for: every pixel of the frame, which both
/// its runs share (`STAGE_TILES`).
uint landingCapacity()
{
    return frame.mEyes.mWorld.mWidth * frame.mEyes.mWorld.mHeight;
}

/// Where the landings' array starts, after the tile lists: one array, which each ray's bounce writes
/// again once the ray before it is read (`STAGE_TILES`).
uint landingListStart()
{
    return STAGE_LISTS * stageTileCapacity();
}

/// The head of the landings of bounce ray `ray`, counting the first as nought: its indirect dispatch.
uint landingDispatch(uint ray)
{
    return STAGE_DISPATCH_LANDING + ray;
}

/// Makes room for `count` pixels in a run of the landings of bounce ray `ray` — the one from the array's
/// start where their gather runs, the one from its end where it is drawn off — and returns the run's first
/// slot: one atomic a workgroup.
uint appendLandings(uint count, bool gathers, uint ray)
{
    const uint lanes = TRACE_WORKGROUP_LANES;
    const uint dispatch = landingDispatch(ray);
    const uint first = gathers ? atomicAdd(stageTiles.mDispatches[dispatch].w, count)
                               : atomicAdd(stageTiles.mLandedApart[ray].x, count);
    const uint groups = (first + count + lanes - 1u) / lanes - (first + lanes - 1u) / lanes;
    if (groups != 0u)
        growDispatch(dispatch, atomicAdd(stageTiles.mLandedApart[ray].y, groups), groups);
    return first;
}

/// Writes `pixel` into `slot` of its run of the landings of bounce ray `ray` (`appendLandings`).
void listLanding(uint slot, bool gathers, uvec2 pixel, uint ray)
{
    const uint at = gathers ? slot : landingCapacity() - 1u - slot;
    stageTiles.mTiles[landingListStart() + at] = pixel.x | pixel.y << 16u;
}

/// The pixel this invocation of the landing's stage of bounce ray `ray` stands for, or false where it
/// stands for none: the workgroup's part of a run of the list, one pixel a lane — the gathering run's
/// workgroups first, then the other's.
bool landingPixelOf(out uvec2 pixel, uint ray)
{
    const uint lanes = TRACE_WORKGROUP_LANES;
    const uint gathering = stageTiles.mDispatches[landingDispatch(ray)].w;
    const uint gatheringGroups = (gathering + lanes - 1u) / lanes;
    const uint group = listedGroup();
    const bool gathers = group < gatheringGroups;
    const uint slot = (gathers ? group : group - gatheringGroups) * lanes + gl_LocalInvocationIndex;
    const bool listed = slot < (gathers ? gathering : stageTiles.mLandedApart[ray].x);
    const uint at = gathers ? slot : landingCapacity() - 1u - slot;
    const uint word = listed ? stageTiles.mTiles[landingListStart() + at] : 0u;
    pixel = uvec2(word & 0xffffu, word >> 16u);
    return listed;
}

/// The eye's ray from `tmin` on, for `pixel`, through the arms' eye or the world's: the query the trace
/// casts for each layer of its peel, and a stage casts again for the solid behind it.
///
/// **Drawn, so back faces culled** — `facingFor`.
///
/// **A see-through surface met as a solid** (`MEET_AS_SOLID`), so the peel composites the layers itself,
/// which a ray that walked past them could not hand it; and a soft edge cut at the pixel's draw a frame
/// (`softEdgeDither`).
RayAsk eyeAsk(vec3 origin, vec3 direction, float tmin, uint arms, uvec2 pixel)
{
    const uint mask = arms != 0u ? frame.mRayMask & MASK_FIRST_PERSON : worldMask(frame.mRayMask);
    const Cone cone = coneAt(eyeOf(arms));
    return rayOf(SceneQuery(origin, tmin, direction, frame.mFar, facingFor(true), mask, MEET_AS_SOLID, 1u,
        cone.mWidth, cone.mSpread, 1u, pixelKey(pixel) + SEED_SOFT_EDGE));
}

/// Stores the solid `asked` met (`met`) at the stored place `stored`: the instance's slot, the triangle and
/// the barycentrics' bits (`STAGE_HIT`); the ray's start with its cone's width where it landed
/// (`STAGE_RAY_FROM`); the ray's direction with the distance (`STAGE_RAY_ALONG`); and how far toward the
/// eye its relief showed its texture, `Surface::mRelief`, or `RELIEF_UNMARCHED` for a hit nothing resolved
/// (`STAGE_RELIEF`), in a frame whose scene has a relief to march at all.
void storeStageSolid(ivec2 stored, SceneAnswer met, SceneQuery asked, float relief)
{
    RTX_STORE_WORDS(stageHit, stored, uvec4(met.mInstance, met.mPrimitive, floatBitsToUint(met.mBary)))
    RTX_STORE_COUNTED(stageRayFrom, stored, vec4(asked.mFrom, footprintOf(asked, met)))
    RTX_STORE_COUNTED(stageRayAlong, stored, vec4(asked.mAlong, met.mDistance))
    if (HAS_RELIEF)
        RTX_STORE_COUNTED(stageRelief, stored, vec4(relief, 0.0, 0.0, 0.0))
}

/// The hit stored at the stored place `stored` (`storeStageSolid`), resolved at the detail `detailed`
/// with no traversal, at the relief its trace marched to, or a landing's (`storedLandingOf`), whose relief is
/// shifted and not marched.
Surface storedHitOf(ivec2 stored, bool detailed, bool landing)
{
    const uvec4 hit = imageLoad(stageHit, stored);
    const vec4 from = imageLoad(stageRayFrom, stored);
    const vec4 along = imageLoad(stageRayAlong, stored);
    Hit committed = committedHit(hit.x, hit.y, uintBitsToFloat(hit.zw), along.w, from.w);
    if (HAS_RELIEF)
        committed.mRelief = imageLoad(stageRelief, stored).r;
    committed.mMarches = !landing;
    return resolve(committed, from.xyz, along.xyz, detailed, RELIEF_STORED);
}

/// The solid `record` names at the stored place `stored`, resolved off its stored hit as the trace resolved
/// it (`storeStageSolid`), with no traversal; no surface for a pixel whose record names none.
///
/// **The hit stored and not the ray traced again**, as Falcor's visibility buffer keeps a hit for the
/// passes after it (`docs/rtx/architecture.md`, "The surface a pixel is the hit the trace stored"): the
/// eye's traversal again cost each stage as much as the whole trace kernel.
Surface storedSolidOf(StageRecord record, ivec2 stored)
{
    if (record.mKind == STAGE_NO_SOLID)
        return noSurface(frame.mOrigin);

    return storedHitOf(stored, record.mDetailed, false);
}

/// Whether bounce ray `ray` stores its landing apart from the solid (`STAGE_LANDING_HIT`): every ray
/// but the frame's last, since a ray after it leaves the solid again. **A branch the whole dispatch
/// takes one way**, on the ray it is pushed and the frame's count.
bool landingApart(uint ray)
{
    return ray + 1u < frame.mBounceRays;
}

/// Stores what bounce ray `ray`'s query `asked` met (`met`) at the stored place `stored`: apart from
/// the solid, laid out as `storeStageSolid` lays out the solid's, where a ray after it leaves the
/// solid again (`landingApart`), and in the solid's place where none does.
void storeLandingHit(ivec2 stored, SceneAnswer met, SceneQuery asked, uint ray)
{
    if (!landingApart(ray))
    {
        storeStageSolid(stored, met, asked, RELIEF_UNMARCHED);
        return;
    }

    RTX_STORE_WORDS(landingHit, stored, uvec4(met.mInstance, met.mPrimitive, floatBitsToUint(met.mBary)))
    RTX_STORE_COUNTED(landingRayFrom, stored, vec4(asked.mFrom, footprintOf(asked, met)))
    RTX_STORE_COUNTED(landingRayAlong, stored, vec4(asked.mAlong, met.mDistance))
}

/// Bounce ray `ray`'s landing at the stored place `stored` (`storeLandingHit`), resolved as the walk
/// that traced it would have resolved it (`resolveAsked`), but for its relief, which is shifted and not
/// marched (`Hit::mMarches`).
Surface storedLandingOf(StageRecord record, ivec2 stored, uint ray)
{
    if (!landingApart(ray))
        return storedHitOf(stored, record.mBounceLobe, true);

    const uvec4 hit = imageLoad(landingHit, stored);
    const vec4 from = imageLoad(landingRayFrom, stored);
    const vec4 along = imageLoad(landingRayAlong, stored);
    Hit landing = committedHit(hit.x, hit.y, uintBitsToFloat(hit.zw), along.w, from.w);
    landing.mMarches = false;
    return resolve(landing, from.xyz, along.xyz, record.mBounceLobe, RELIEF_STORED);
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
