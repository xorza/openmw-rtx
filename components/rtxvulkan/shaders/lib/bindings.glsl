#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BINDINGS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BINDINGS_GLSL

// Everything the trace is handed: set nought's bindings, and every table the scene owns reached through an
// address the frame block carries.
//
// The frame's own block carries what is not a table, and where every table is: a record that is one row
// belongs in the block rather than in a descriptor of its own, and the tables ride there as `GpuTables`,
// one address apiece, because seventeen storage-buffer bindings were seventeen places a layout and a write
// could disagree; `scene.h` says the rest.
//
// A table is read through a function and never through its reference: `instanceAt`, `lightAt` and their
// siblings construct the reference from the block, so the alignment each claims is stated once, beside the
// table. None takes `nonuniformEXT`, which is for an index into a descriptor array, not a reference.
//
// The declarations are here and `bindings.h` holds the numbers: set zero's are shared with
// `VisibilityPass`, which writes the same slots.
//
// Four sets, by who made what they name: zero is the frame and what it points at, pushed; one is the
// bindless textures a scene owns; two is the channels a `GBuffer` owns (`gbuffer.h`); three is the air a
// `FogVolume` holds (`fogvolume.h`). The device allows 32 push descriptors and set zero had reached
// exactly 32, so every list that keeps growing moved to the owner that already holds it.

// The structure below is a query's, and every traversal in the libraries this reaches is a
// query. Declared here and not in each stage, so a stage cannot reach the structure without it.
#extension GL_EXT_ray_query : require

#include "shared/tables.h"
#include "shared/bindings.h"
#include "brdf.h"
#include "shared/counts.h"
#include "shared/fogvolume.h"
#include "gbuffer.h"
#include "shared/glare.h"
#include "scene.h"
#include "shared/sets.h"
#include "visibility.h"
#include "wave.h"

#include "ground.glsl"
#include "spritelist.glsl"
#include "texturearray.glsl"

layout(set = SET_PASS, binding = BIND_SCENE) uniform accelerationStructureEXT sceneTop;

// Set two, in the order it is bound.

/// Everything already resolved: direct light, emission, the sky, water, and the fog over all of it.
///
/// No format on this or the bounce below, because a run decides how wide they are (`gbuffer.h`) and a
/// store with no format converts to whatever the view holds. Readable, as every channel the bounce's
/// stages weigh the pane mask against is (`storeBounced`).
layout(set = SET_CHANNELS, binding = CHANNEL_DIRECT) uniform image2D direct;

/// The lamps and the one bounce with the albedo divided out, times whatever the path took off it on the way
/// to the eye — the channel the wavelet filters.
///
/// Demodulated because a blur must not touch texture: dividing the albedo out leaves the slowly varying
/// light, and the composite multiplies the wall back in at full sharpness.
///
/// The water and the air ride here rather than with the albedo. Both are `colour * a + b` over everything
/// in front of the eye, so applying them to a sum applies them to each term: `b` goes into `direct` and `a`
/// belongs to whichever term it attenuated, which is this one. On the albedo it would be a product of a
/// surface and a path, and a filter asking what the surface is would get the weather.
layout(set = SET_CHANNELS, binding = CHANNEL_INDIRECT) uniform image2D indirect;

/// The surface's own diffuse albedo, and nothing else.
layout(set = SET_CHANNELS, binding = CHANNEL_ALBEDO, GBUFFER_ALBEDO) uniform image2D albedo;

/// The shading normal as `packSurfaceNormal`'s code in `r`, and the distance from the eye along the pixel's
/// ray in `g`.
///
/// The normal the shading actually used, which for water is the wave's rather than the plane's.
layout(set = SET_CHANNELS, binding = CHANNEL_SURFACE, GBUFFER_SURFACE) uniform image2D surfaceChannel;

/// Where each surface stood on the previous frame's screen, less where it stands on this one. Read
/// back by the denoiser's passes for where each history is, which is why it is not `writeonly`.
layout(set = SET_CHANNELS, binding = CHANNEL_MOTION, GBUFFER_MOTION) uniform image2D motion;

/// How much of the backdrop this pixel still shows, per channel, in `rgb` — everything the trace put
/// between the backdrop and the eye, multiplied together.
///
/// The backdrop is drawn by a pass that can see none of this (`ToneConstants::mStars` says why), with no
/// moons, cloud deck, window pane, water or fog in front of it. So the trace hands it the one number that
/// carries all of them: `skyRadiance`'s `shown` times the path's own transmittance, the puffs' apart, which
/// `spritecomposite.rgen` leaves in the frame's alpha for the same pass.
///
/// In `a`, what see-through arms let through of everything behind them, one where none stands.
layout(set = SET_CHANNELS, binding = CHANNEL_BACKDROP, GBUFFER_BACKDROP) uniform image2D backdrop;

/// The sprites in front of the surface: their straight colour lit where they stand and already
/// fog-attenuated, and what they let through in `a`.
/// Read back by `spritecomposite.rgen`, which is why it is not `writeonly`.
layout(set = SET_CHANNELS, binding = CHANNEL_PUFFS, GBUFFER_LAYER) uniform image2D puffs;

/// What the sky's source adds to what the eye found, as though its ray got through, times what the path
/// took off it, and in `a` whether the ray got through and its penumbra — `CHANNEL_SHADOWED` — and the same
/// for the lamps, `CHANNEL_LAMPED`.
layout(set = SET_CHANNELS, binding = CHANNEL_SHADOWED) uniform image2D shadowed;
layout(set = SET_CHANNELS, binding = CHANNEL_LAMPED) uniform image2D lamped;

/// What the lobe reflects of the lamps and the bounce, times what the path took off it —
/// `CHANNEL_SPECULAR`. No format, as radiance has none here.
layout(set = SET_CHANNELS, binding = CHANNEL_SPECULAR) uniform image2D specular;

/// The see-through layers' drawn light, demodulated, and what puts it back: `CHANNEL_PANE` and
/// `CHANNEL_PANE_ALBEDO`. The light has no format, as radiance has none here.
layout(set = SET_CHANNELS, binding = CHANNEL_PANE) uniform image2D pane;
layout(set = SET_CHANNELS, binding = CHANNEL_PANE_ALBEDO, GBUFFER_ALBEDO) uniform image2D paneAlbedo;

/// The nearest layer's own surface and motion — `CHANNEL_PANE_SURFACE` and `CHANNEL_PANE_MOTION`.
layout(set = SET_CHANNELS, binding = CHANNEL_PANE_SURFACE, GBUFFER_SURFACE) uniform writeonly image2D paneSurface;
layout(set = SET_CHANNELS, binding = CHANNEL_PANE_MOTION, GBUFFER_MOTION) uniform writeonly image2D paneMotion;

/// What the upscaler is told its motion vector does not describe — `CHANNEL_UPSCALE_MASKS`.
layout(set = SET_CHANNELS, binding = CHANNEL_UPSCALE_MASKS, GBUFFER_UPSCALE_MASKS) uniform writeonly image2D
    upscaleMasks;

/// The share of the bounce that is the fill, demodulated and taken down by the path as `indirect`
/// is, and the ambient albedo that puts it back — `CHANNEL_FILL` and `CHANNEL_AMBIENT_ALBEDO`. The
/// light has no format, as radiance has none here.
layout(set = SET_CHANNELS, binding = CHANNEL_FILL) uniform image2D fill;
layout(set = SET_CHANNELS, binding = CHANNEL_AMBIENT_ALBEDO, GBUFFER_ALBEDO) uniform image2D ambientAlbedo;

/// What Night-Eye's lift is multiplied by — `CHANNEL_LIFT`.
layout(set = SET_CHANNELS, binding = CHANNEL_LIFT, GBUFFER_LIFT) uniform image2D lift;

/// What the lobe's light is multiplied back by — `CHANNEL_SPECULAR_ALBEDO`.
layout(set = SET_CHANNELS, binding = CHANNEL_SPECULAR_ALBEDO, GBUFFER_ALBEDO) uniform image2D specularAlbedo;

/// What the trace hands its later stages: each pixel's `StageRecord`, and the throughput to its
/// solid — `STAGE_RECORD` and `STAGE_THROUGHPUT`, what the media in front and a water path let
/// through in `rgb` and what the layers let through in `a`.
layout(set = SET_CHANNELS, binding = STAGE_RECORD, GBUFFER_STAGE_RECORD) uniform uimage2D stageRecord;
layout(set = SET_CHANNELS, binding = STAGE_THROUGHPUT, GBUFFER_STAGE_THROUGHPUT) uniform image2D stageThroughput;

/// The lift of the surface behind the layers, as halves (`STAGE_LIFT`).
layout(set = SET_CHANNELS, binding = STAGE_LIFT, GBUFFER_STAGE_LIFT) uniform uimage2D stageLift;

/// The solid's hit and the ray that met it (`STAGE_HIT`, `STAGE_RAY_FROM`, `STAGE_RAY_ALONG`):
/// `storeStageSolid` says what each holds.
layout(set = SET_CHANNELS, binding = STAGE_HIT, GBUFFER_STAGE_HIT) uniform uimage2D stageHit;
layout(set = SET_CHANNELS, binding = STAGE_RAY_FROM, GBUFFER_STAGE_RAY) uniform image2D stageRayFrom;
layout(set = SET_CHANNELS, binding = STAGE_RAY_ALONG, GBUFFER_STAGE_RAY) uniform image2D stageRayAlong;

/// What the bounce hands its landing (`STAGE_BOUNCE`): `storeBounce` says what it holds.
layout(set = SET_CHANNELS, binding = STAGE_BOUNCE, GBUFFER_STAGE_BOUNCE) uniform uimage2D stageBounce;

/// The bounce's landing, laid out as the solid's hit and its ray (`storeLandingHit`), for every bounce
/// ray but the frame's last: a texel wide in a frame of one ray, which reads none of them.
layout(set = SET_CHANNELS, binding = STAGE_LANDING_HIT, GBUFFER_STAGE_HIT) uniform uimage2D landingHit;
layout(set = SET_CHANNELS, binding = STAGE_LANDING_RAY_FROM, GBUFFER_STAGE_RAY) uniform image2D landingRayFrom;
layout(set = SET_CHANNELS, binding = STAGE_LANDING_RAY_ALONG, GBUFFER_STAGE_RAY) uniform image2D landingRayAlong;

/// How far toward the eye the solid's relief showed its texture (`STAGE_RELIEF`): `storeStageSolid` says
/// what it holds.
layout(set = SET_CHANNELS, binding = STAGE_RELIEF, GBUFFER_STAGE_RELIEF) uniform image2D stageRelief;

/// What the sun's shafts made of the eye's water column (`STAGE_SHAFTS`): `storeStageShafts` says what it
/// holds.
layout(set = SET_CHANNELS, binding = STAGE_SHAFTS, GBUFFER_STAGE_SHAFTS) uniform uimage2D stageShafts;

/// The tiles the later stages run over, which the trace appends, and the pixels the landing's stage runs
/// over, which the bounce's appends (`STAGE_TILES`): each tile list's count in its indirect dispatch's `x`,
/// which the frame's `GBuffer::begin` sets to nought, and each tile list `stageTileCapacity` long; each
/// bounce ray's landings counted in its word beside, and their array a pixel of the frame long.
layout(set = SET_CHANNELS, binding = STAGE_TILES, std430) buffer StageTiles
{
    uvec4 mDispatches[STAGE_DISPATCHES];

    /// For each bounce ray, in `x`, how many landings are listed from the end, and in `y`, how many
    /// workgroups both runs take (`appendLandings`).
    uvec2 mLandedApart[MAX_BOUNCE_RAYS];
    uint mTiles[];
}
stageTiles;

// The trace's counts, added to one atomic at a time where a ray ends: `countMiss` says why the
// misses and not the hits, which a room of nothing but hits made cost.
layout(set = SET_PASS, binding = BIND_COUNTS, scalar) buffer Counted
{
    FrameCounts counts;
};

/// The sun glare fader's query, as `glare.h` states it: two atomics, added to by every primary
/// ray inside the quad's disc. Few rays: the disc is a sliver of the frame, so an atomic a ray there
/// is what the counts measured cheap.
layout(set = SET_PASS, binding = BIND_SUN_GLARE, scalar) buffer SunGlare
{
    SunGlareCount sunGlare;
};

// A buffer and not a push constant: the frame's description passed 256 bytes, every byte
// `maxPushConstantsSize` promises on this hardware; `VisibilityPass` writes it into a buffer of its own.
//
// Uniform and not storage, worth a few per cent of the trace: every pixel reads half of these fields
// several times over, and a uniform block is promoted to a constant bank the way a push constant is.
//
// Declared before the tables, because the tables are reached through it.
layout(set = SET_PASS, binding = BIND_FRAME, scalar) uniform Frame
{
    VisibilityConstants frame;
};

// The scene's tables, each a reference constructed from the address `frame.mTables` carries.
//
// The alignment each reference claims is `scene.h`'s to state, beside the rows it is about:
// `TABLE_ALIGN_LAYERS` where a 64-byte row puts two `vec4` on sixteen, `TABLE_ALIGN_BLOCKS` for a table of
// eight-byte addresses, and `TABLE_ALIGN_ROWS` everywhere a row or an element is only four-aligned.
//
// Not `restrict`, though it would be true: tried on every reference, it gave back a hundredth or two of the
// four the address path costs the guild's trace, inside the noise, and it moved two interiors where the
// compiled shape shifted a lamp pick.

// The vertex attributes and the indices, as lists of blocks.
//
// A block is allocated once at its full size and never moved, so a scene that grows keeps every address
// already handed out and every acceleration structure built from one stays valid: a cell arriving appends
// rather than rebuilds the world.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer PositionBlock
{
    vec3 at[];
};

// A vertex's normal as one word — `packNormal` in `tangent.h` — and nought where the mesh has none.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer NormalBlock
{
    uint at[];
};

layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer TexCoordBlock
{
    vec2 at[];
};

// The per-vertex colour, in linear light as halves (`GpuVertexColour`) and white where a mesh brought
// none — `Rtx::MeshArrays` says why the decode is the host's and not this side's.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer ColourBlock
{
    GpuVertexColour at[];
};

layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer IndexBlock
{
    uint at[];
};

// A vertex's tangent as one word — `TANGENT_*` in `scene.h` — and nought where the mesh has none.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer TangentBlock
{
    uint at[];
};

/// Where a blocked table's blocks start: a table of addresses, one per block.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_BLOCKS) readonly buffer BlockTable
{
    uint64_t at[];
};

// The block a global id lives in.
//
// Resolved once for a triangle and not once for a corner: a mesh's run never straddles a block, so its three
// corners and indices share one, and the table read that finds it is the same for all three.
// `geometry.glsl` takes the block and reads the corners out of it.
NormalBlock normalBlockOf(uint vertex)
{
    return NormalBlock(BlockTable(frame.mTables.mNormalBlocks).at[vertex / VERTEX_BLOCK]);
}

/// The static meshes' positions, laid out as the colours are, by the global vertex id.
PositionBlock positionBlockOf(uint vertex)
{
    return PositionBlock(BlockTable(frame.mTables.mPositionBlocks).at[vertex / VERTEX_BLOCK]);
}

/// The pose blocks are laid out as the normals are — `SlotBlocks` of `VERTEX_BLOCK` positions —
/// so a `PositionBlock` reads one; what differs is the id, which is the mesh's bind offset plus
/// the vertex's index within the mesh, and the table, this frame's copy or the last one's.
PositionBlock poseBlockOf(uint posed)
{
    return PositionBlock(BlockTable(frame.mTables.mPoseBlocks).at[posed / VERTEX_BLOCK]);
}

PositionBlock previousPoseBlockOf(uint posed)
{
    return PositionBlock(BlockTable(frame.mTables.mPreviousPoseBlocks).at[posed / VERTEX_BLOCK]);
}

TangentBlock tangentBlockOf(uint vertex)
{
    return TangentBlock(BlockTable(frame.mTables.mTangentBlocks).at[vertex / VERTEX_BLOCK]);
}

TexCoordBlock texCoordBlockOf(uint vertex)
{
    return TexCoordBlock(BlockTable(frame.mTables.mTexCoordBlocks).at[vertex / VERTEX_BLOCK]);
}

/// The block a mesh's second set lives in, by the global id in the second set's own blocks.
TexCoordBlock secondTexCoordBlockOf(uint vertex)
{
    return TexCoordBlock(BlockTable(frame.mTables.mSecondTexCoordBlocks).at[vertex / VERTEX_BLOCK]);
}

ColourBlock colourBlockOf(uint vertex)
{
    return ColourBlock(BlockTable(frame.mTables.mColourBlocks).at[vertex / VERTEX_BLOCK]);
}

IndexBlock indexBlockOf(uint element)
{
    return IndexBlock(BlockTable(frame.mTables.mIndexBlocks).at[element / INDEX_BLOCK]);
}

layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer MeshTable
{
    GpuMesh at[];
};

layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer InstanceTable
{
    GpuInstance at[];
};

layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer LightTable
{
    GpuLight at[];
};

layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer LightCellTable
{
    GpuLightCell at[];
};

/// A list of `uint`: the light grid's.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer IndexList
{
    uint at[];
};

layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer BlueNoiseTable
{
    float at[];
};

/// The lobe's two integrals a cell — `Rtx::SpecularAlbedo`.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer SpecularAlbedoTable
{
    vec2 at[];
};

GpuMesh meshAt(uint index)
{
    return MeshTable(frame.mTables.mMeshes).at[index];
}

GpuInstance instanceAt(uint index)
{
    return InstanceTable(frame.mTables.mInstances).at[index];
}

/// Three rows of four as the matrix they are, the mesh's own space to the world or back.
mat4x3 transformOf(vec4 rows[3])
{
    return transpose(mat3x4(rows[0], rows[1], rows[2]));
}

mat4x3 toWorldOf(GpuInstance instance)
{
    return transformOf(instance.mToWorld);
}

mat4x3 toObjectOf(GpuInstance instance)
{
    return transformOf(instance.mToObject);
}

GpuMaterial materialAt(uint index)
{
    return MaterialTable(frame.mTables.mMaterials).at[index];
}

GpuLayer layerAt(uint index)
{
    return LayerTable(frame.mTables.mLayers).at[index];
}

GpuLight lightAt(uint index)
{
    return LightTable(frame.mTables.mLights).at[index];
}

GpuLightCell lightCellAt(uint cell)
{
    return LightCellTable(frame.mTables.mLightCells).at[cell];
}

/// One lamp of the light grid's list, which the cells' runs stand in — `Rtx::LightGrid::getCells`.
uint lightListAt(uint slot)
{
    return IndexList(frame.mTables.mLightList).at[slot];
}

/// The blue-noise tile, `RANDOM_STREAMS` channels interleaved per pixel. See `Rtx::BlueNoise`.
float blueNoiseAt(uint index)
{
    return BlueNoiseTable(frame.mTables.mBlueNoise).at[index];
}

/// The lobe's two integrals at one cell of the table, cell by cell along the cosine to the eye and
/// row by row along the roughness.
vec2 specularAlbedoCell(uint column, uint row)
{
    return SpecularAlbedoTable(frame.mTables.mSpecularAlbedo).at[row * SPECULAR_TABLE_SIZE + column];
}

/// The frame's texel counts, for a function that takes them because another pass calls it too.
TexelTable sceneTexels()
{
    return TexelTable(frame.mTables.mTextureTexels);
}

/// How many texels the texture in `slot` holds, which is what its mip level owes its own size.
uint textureTexelsAt(uint slot)
{
    return sceneTexels().at[slot] & TEXTURE_TEXELS;
}

/// Whether `slot` holds what was named for it, in this frame's array.
bool holdsTexture(uint slot)
{
    return holdsTexture(sceneTexels(), slot);
}

/// Every live particle in the scene, one emitter's run after another's.
GpuSprite spriteAt(uint index)
{
    return SpriteTable(frame.mTables.mSprites).at[index];
}

/// One sphere and one run of sprites per particle system, indexed by `GpuSprite::mEmitter`.
///
/// No count beside it, because nothing walks these: the only way in is from a sprite the tile named, which
/// can only name a real one.
GpuEmitter emitterAt(uint index)
{
    return EmitterTable(frame.mTables.mEmitters).at[index];
}

/// What this trace made of the emitter at `index`, `GpuEmitterFrame`.
GpuEmitterFrame emitterFrameAt(uint index)
{
    return EmitterFrames(frame.mTables.mEmitterFrames).at[index];
}

/// The sprite tiles' list, in the light grid's shape over the screen's tiles: where each tile's run starts,
/// then every tile's sprites run together in tile order and ascending inside each run — which is the order
/// they composite in.
///
/// Made on the device, by `SpriteBinPass`, ahead of the trace. `spriterects.comp` says why the layer is
/// binned per tile and the emitters are not, and `SPRITE_TILE_UNBINNED` what a tile past the room holds.
uint spriteTileListAt(uint slot)
{
    return SpriteTileList(frame.mTables.mSpriteTileList).at[slot];
}

// The sea, as the tiles `WavePass` synthesised it into. One texture apiece per cascade, sampled rather than
// loaded, because the level a ray cone reaches is what a water pixel asks for.
//
// Three textures and not one, because the third is a square that has to be averaged apart from what it is
// the square of: the mean of `tr(H)^2` over a footprint and the square of the mean of `tr(H)` differ by the
// curvature the cone threw away.

/// The two slopes, their own second moment, and the elevation squared.
layout(set = SET_PASS, binding = BIND_WAVE_SURFACE) uniform sampler2D waveSurface[WAVE_CASCADES];

/// The three curvatures.
layout(set = SET_PASS, binding = BIND_WAVE_CURVATURE) uniform sampler2D waveCurvature[WAVE_CASCADES];

/// The ripple field, in the wave tiles' own layout and read the same way, once: it is anchored to
/// the world at `frame.mRippleOrigin` rather than repeating, and past its edge reads as still
/// water through a sampler that clamps to nothing.
layout(set = SET_PASS, binding = BIND_RIPPLE_SURFACE) uniform sampler2D rippleSurface;
layout(set = SET_PASS, binding = BIND_RIPPLE_CURVATURE) uniform sampler2D rippleCurvature;

/// The fog's fractal field, drawn once for the life of the device and read at three world scales.
///
/// Wrapping, mipped, and two channels: `.x` is the shape a coverage band is cut out of and `.y` a second
/// field decorrelated from it, read together as the displacement the finer scales are sampled at, a vector
/// out of one fetch. A volume and not a ground plan, for the reason `FOG_FIELD_SIZE` gives.
layout(set = SET_PASS, binding = BIND_FOG_FIELD) uniform sampler3D fogField;

// The air in front of the eye, integrated once for a block of pixels rather than once per pixel.
// `Rtx::FogVolume` says what each image holds and why there are three pairs of them.
//
// A set of its own, the one `GBuffer` already argued for: set zero is pushed and the device allows 32 push
// descriptors, so images belonging to a camera's size go with the owner that holds them. Each image is
// named twice wherever a pass both reads and writes it, because Vulkan has no descriptor that is both.

/// What the air scatters at a point in `rgb` and its extinction per world unit in `a`, as the previous
/// frame left it — and beside it the three answers a ray each gave there: the sun's transport in `r`, the
/// lamp's seeing in `g` and the ambient's in `b`.
layout(set = SET_VOLUME, binding = BIND_FOG_WAS_SCATTER) uniform sampler3D fogWasScatter;
layout(set = SET_VOLUME, binding = BIND_FOG_WAS_SUNWARD) uniform sampler3D fogWasSunward;

/// The same two as this frame's scatter pass wrote them, which is what its integrate pass reads.
layout(set = SET_VOLUME, binding = BIND_FOG_SCATTER) uniform sampler3D fogScatter;
layout(set = SET_VOLUME, binding = BIND_FOG_SUNWARD) uniform sampler3D fogSunward;

/// What every lamp puts into a froxel, per steradian and with nothing standing in the way — read by
/// the integrate pass beside the seeing above it, and by a puff for the same product.
layout(set = SET_VOLUME, binding = BIND_FOG_LAMPS) uniform sampler3D fogLamps;

/// Both accumulated front to back, which is what a pixel reads. `a` of the first is what is left of
/// a ray at that depth; the second is the sun's transport alone, one channel.
layout(set = SET_VOLUME, binding = BIND_FOG_AIR) uniform sampler3D fogVolumeAir;
layout(set = SET_VOLUME, binding = BIND_FOG_AIR_SUNWARD) uniform sampler3D fogVolumeSunward;

/// What each slice holds once everything that lights it is applied — `FogSlice`, as the two images
/// it packs into — which is what a pixel steps through from the last edge it passed to where its
/// surface stands.
layout(set = SET_VOLUME, binding = BIND_FOG_SLICE) uniform sampler3D fogSlice;
layout(set = SET_VOLUME, binding = BIND_FOG_SLICE_SUNWARD) uniform sampler3D fogSliceSunward;

/// The three answers `fogSunward` holds, with the neighbours across the screen averaged in exactly as the
/// integrate pass averages them for the air — its `sliceAt` says why — which is what a puff of smoke is lit
/// by at a point, `puffLight` being the one thing in the trace that wants a froxel's own answer rather than
/// a column's integral of it.
layout(set = SET_VOLUME, binding = BIND_FOG_SEEING) uniform sampler3D fogSeeing;

/// The same eight, as the pass that fills each one writes it.
layout(set = SET_VOLUME, binding = BIND_FOG_SCATTER_TARGET, FOG_HISTORY_FORMAT)
    uniform writeonly image3D fogScatterTarget;
layout(set = SET_VOLUME, binding = BIND_FOG_SUNWARD_TARGET, FOG_HISTORY_FORMAT)
    uniform writeonly image3D fogSunwardTarget;
layout(set = SET_VOLUME, binding = BIND_FOG_LAMPS_TARGET, FOG_VOLUME_FORMAT) uniform writeonly image3D fogLampsTarget;
layout(set = SET_VOLUME, binding = BIND_FOG_AIR_TARGET, FOG_VOLUME_FORMAT) uniform writeonly image3D fogVolumeAirTarget;
layout(set = SET_VOLUME, binding = BIND_FOG_AIR_SUNWARD_TARGET, FOG_SUNWARD_FORMAT)
    uniform writeonly image3D fogVolumeSunwardTarget;
layout(set = SET_VOLUME, binding = BIND_FOG_SLICE_TARGET, FOG_VOLUME_FORMAT) uniform writeonly image3D fogSliceTarget;
layout(set = SET_VOLUME, binding = BIND_FOG_SLICE_SUNWARD_TARGET, FOG_SUNWARD_FORMAT)
    uniform writeonly image3D fogSliceSunwardTarget;
layout(set = SET_VOLUME, binding = BIND_FOG_SEEING_TARGET, FOG_VOLUME_FORMAT) uniform writeonly image3D fogSeeingTarget;

/// How far each column's ray runs before it meets a surface, which `fogdepth.rgen` writes and the
/// scatter pass reads. **One storage binding for both**, because neither samples it: a column reads
/// its own texel and nothing between texels.
layout(set = SET_VOLUME, binding = BIND_FOG_COLUMN_DEPTH, FOG_DEPTH_FORMAT) uniform image2D fogColumnDepth;

/// What each moon puts into the air along each column's ray, one layer a moon, which
/// `fogdepth.rgen` writes and the scatter pass reads. `FogVolume::mColumnMoons` says why it is the
/// column's and not the froxel's.
layout(set = SET_VOLUME, binding = BIND_FOG_COLUMN_MOONS, FOG_MOONS_FORMAT) uniform image3D fogColumnMoons;

#endif
