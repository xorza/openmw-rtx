#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_TEXTUREARRAY_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_TEXTUREARRAY_GLSL

// The scene's textures, and nothing else.
//
// **Its own file because a second pass samples them.** The trace has this by way of everything else it is
// handed; the display pass draws the sky's own points at the resolution they are shown at, and needs the
// sheet and none of the rest of `bindings.glsl`. The set and its bindings are the host's as well, so both
// sides read them from `sets.h` and `scene.h`.

// **The extension travels with the declaration**, because what needs it is the indexing rather than
// the pass: a shader that includes this and forgets the line fails to compile, which is the failure
// worth having.
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

#include "shared/tables.h"
#include "scene.h"
#include "shared/sets.h"

/// Every texture the scene loaded, indexed by the slot a material, a layer or an emitter names — and at
/// `TEXTURE_NEUTRAL` the one texel a material with no diffuse names instead. An image alone: a read pairs it
/// with the sampler its wrap names (`TEXTURE`, `TEXTURE_ALONG`).
///
/// **A slot is qualified where it indexes and never where it is passed.** Neighbouring lanes hit different
/// materials over most of a frame, so the descriptor read has to be a waterfall, which the driver emits only
/// for a `NonUniform` decoration on the access chain itself: `nonuniformEXT` on a function *argument*
/// decorates the argument and stops there, and the chain built in the callee comes out bare, so the driver may
/// read one lane's descriptor for the whole wave. The pairs are macros for that reason, since a function
/// returns no sampler.
///
/// **Images and samplers apart, and not a combined image sampler per slot and per filter.** Through the
/// combined footprint array, AMD's Windows driver hung the trace in the waterfall of a specular map's read
/// (`docs/rtx/research/2026-10-22-amd-integrated-footprint-read-hang.md`), and the specular maps were read at
/// their long axis's level to stand clear of it; read as a pair they pass along the footprint. A change of
/// anisotropy also writes the eight samplers and not a descriptor for every slot.

layout(set = SET_TEXTURES, binding = TEXTURE_BIND_IMAGES) uniform texture2D textures[];

/// What was measured on each texture as it arrived (`Rtx::TextureCompanion`), at the slot of the texture it
/// was measured on, and read through the same sampler (`COMPANION`): for a normal map, what each of its
/// levels lost, which `normalMapSlopes` reads, and nothing at any other slot, which no read reaches.
///
/// **A binding of its own and not slots between the textures**, because `coneLod` measures the array it
/// reads for the level a cone resolves, and a companion interleaved with the textures is one it would
/// measure.
layout(set = SET_TEXTURES, binding = TEXTURE_BIND_COMPANIONS) uniform texture2D companions[];

/// The samplers a read pairs an image with, by the slot's wrap: `TEXTURE_SAMPLERS_ALONG` says which.
///
/// **The footprint's four apart from the level's, and not anisotropy on every read**, because the device
/// does not leave a read that names its level alone under such a sampler: an explicit level through one
/// came back blended at a different fraction of a level than it asked for, and every read but the eye's
/// names its level.
layout(set = SET_TEXTURES, binding = TEXTURE_BIND_SAMPLERS) uniform sampler textureSamplers[TEXTURE_SAMPLERS];

/// How many texels each slot holds, with the slot's wrap over the count (`TEXTURE_WRAP_SHIFT`) and
/// `TEXTURE_STANDS_IN` over both where the slot draws the stand-in — `GpuTables::mTextureTexels`, handed
/// to each pass by address.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer TexelTable
{
    uint at[];
};

/// Whether `slot` holds what was named for it: a slot at all, and not one the backend stands in
/// for. What a reader of an optional map asks before the read — `TEXTURE_STANDS_IN` says why.
bool holdsTexture(TexelTable texels, uint slot)
{
    return slot != NO_TEXTURE && (texels.at[slot] & TEXTURE_STANDS_IN) == 0u;
}

/// The sampler of `textureSamplers` that reads `slot` at a named level: the one its wrap names.
uint samplerAt(TexelTable texels, uint slot)
{
    return (texels.at[slot] >> TEXTURE_WRAP_SHIFT) & (TEXTURE_SAMPLERS_ALONG - 1u);
}

/// `slot`'s texture through the sampler its wrap names, for a read that names its level. `slot` is read
/// twice, so it is a name or a plain expression.
#define TEXTURE(texels, slot)                                                                                      \
    nonuniformEXT(sampler2D(textures[nonuniformEXT(slot)], textureSamplers[nonuniformEXT(samplerAt(texels, slot))]))

/// `slot`'s texture through the sampler that filters along a footprint, for a read that hands its
/// gradients (`textureGrad`).
#define TEXTURE_ALONG(texels, slot)                                                                                \
    nonuniformEXT(sampler2D(textures[nonuniformEXT(slot)],                                                         \
        textureSamplers[nonuniformEXT(TEXTURE_SAMPLERS_ALONG + samplerAt(texels, slot))]))

/// `slot`'s companion through the sampler its texture is read through.
#define COMPANION(texels, slot)                                                                                    \
    nonuniformEXT(                                                                                                 \
        sampler2D(companions[nonuniformEXT(slot)], textureSamplers[nonuniformEXT(samplerAt(texels, slot))]))

#endif
