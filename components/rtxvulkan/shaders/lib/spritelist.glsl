#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SPRITELIST_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SPRITELIST_GLSL

// The sprite tables and the tiles' list over them, as the three bin passes write them and the
// trace reads them.
//
// **One statement of the list's shape, because four shaders indexed it by hand.** The count pass
// and the scan wrote at `1 + tile`, the fill and the trace read at `tile` and `tile + 1`, and the
// rule that makes those one table — entry nought is the head — was prose in `scene.h`. The rect a
// sprite covers was packed in one pass and unpacked in another, sixteen bits a coordinate, with
// the split spelled in both. And each pass declared the tables again with the alignment as a
// literal, where `scene.h` names what each reference may claim.
//
// Nothing here reads the frame, so a pass with no frame block reaches it.

#include "shared/tables.h"
#include "camera.h"
#include "scene.h"

layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer SpriteTable
{
    GpuSprite at[];
};

layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer EmitterTable
{
    GpuEmitter at[];
};

/// What a trace made of each emitter, `GpuEmitterFrame`: written by `spriteemitters.rgen` and read
/// by the walks.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) buffer EmitterFrames
{
    GpuEmitterFrame at[];
};

/// The same sprites, for the two passes that write them: the shelter zeroes a drop under a roof
/// and the shade writes each sprite's layers.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) buffer WrittenSprites
{
    GpuSprite at[];
};

/// The placement's medium and additive instances, as the spheres they can be met in.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) readonly buffer PresenceTable
{
    GpuPresence at[];
};

/// One word of `PRESENCE_` bits a tile, `GpuTables::mSpritePresence`.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) buffer SpritePresence
{
    uint at[];
};

/// One packed rect per sprite, which `spriterects.comp` writes and `spriteruns.comp` reads.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_BLOCKS) buffer SpriteRects
{
    uvec2 at[];
};

/// The tiles' list: entry nought is the head, then where each tile's run starts, counted from the
/// front, then the runs. `SPRITE_TILE_UNBINNED` says what a tile past the room holds.
layout(buffer_reference, scalar, buffer_reference_align = TABLE_ALIGN_ROWS) buffer SpriteTileList
{
    uint at[];
};

/// Where `tile`'s count accumulates while the sprites are binned. The scan turns that entry in
/// place into where tile `tile + 1`'s run starts, and writes the head into entry nought — which is
/// also where tile nought's run starts, and is what makes the two readings one table.
uint spriteCountSlot(uint tile)
{
    return 1u + tile;
}

/// Where `tile`'s run starts once the scan has run, and `spriteStartSlot(tile + 1)` is where it
/// ends.
uint spriteStartSlot(uint tile)
{
    return tile;
}

/// What a walk over one tile reads: the slots `[mSlot, mEnd)`, and whether they name sprites
/// outright rather than entries of the list — a tile with no run, which walks every sprite.
struct SpriteRun
{
    uint mSlot;
    uint mEnd;
    bool mUnbinned;
};

/// `tile`'s run, `SpriteRun`: its own where it was binned, and every sprite where it was not.
SpriteRun spriteRunOf(SpriteTileList list, uint tile)
{
    const uint end = list.at[spriteStartSlot(tile + 1u)];
    if ((end & SPRITE_TILE_UNBINNED) != 0u)
        return SpriteRun(0u, end & ~SPRITE_TILE_UNBINNED, true);

    return SpriteRun(list.at[spriteStartSlot(tile)], end, false);
}

/// Which tile a traced pixel is in, on a frame `width` pixels across.
uint spriteTileOf(uvec2 pixel, uint width)
{
    return (pixel.y / SPRITE_TILE) * spriteTilesOver(width) + pixel.x / SPRITE_TILE;
}

/// The `PRESENCE_` kinds a ray through a traced pixel's tile can meet — every kind where the tile
/// was not binned, which is a tile past the room.
///
/// **Uniform over a tile, so a warp takes a walk or leaves it whole.**
uint presenceAt(SpriteTileList list, SpritePresence presence, uint tracedWidth, uvec2 traced)
{
    const uint tile = spriteTileOf(traced, tracedWidth);
    return spriteRunOf(list, tile).mUnbinned ? PRESENCE_ADDITIVE | PRESENCE_MEDIUM : presence.at[tile];
}

/// Whether the puff layer holds nothing at a traced pixel: no sprite binned into its tile, no cloud
/// shell and no additive mesh a ray through its tile can meet, and not the arms' — so the composite
/// there would leave the frame as it found it, with a transmittance of one in its alpha. An arms'
/// ray looks its sprites up in another tile than its own (`binnedPixel`), and the hand is a sliver
/// of the frame, so it is walked rather than asked about.
///
/// **Asked by the composite and by the curve, which have to agree pixel for pixel.** The composite
/// skips such a pixel, and the curve reads a transmittance of one there in place of an alpha the
/// composite never wrote. That is most of every frame at the shown extent: on a clear day every
/// pixel paid three loads and a store to write a one, which was 0.46 ms of a 4K frame and 0.27
/// with the writes gone. A tile past the room was not binned, and every pixel of it walks every
/// sprite — `SPRITE_TILE_UNBINNED`.
///
/// **Of the tile alone, and of nothing a ray through the pixel found**, so the answer is the same
/// whichever point of the pixel the trace sampled.
///
/// @param arms whether the trace drew the pixel on an arm, `surfaceOnArms`.
bool puffsCoverNothing(SpriteTileList list, SpritePresence presence, uint tracedWidth, uvec2 traced, bool arms)
{
    const uint tile = spriteTileOf(traced, tracedWidth);
    const SpriteRun run = spriteRunOf(list, tile);
    if (run.mUnbinned || arms)
        return false;

    return (presence.at[tile] & (PRESENCE_ADDITIVE | PRESENCE_MEDIUM)) == 0u && run.mSlot == run.mEnd;
}

/// A tile rect as one `uvec2`: the corner in `x` and the far corner in `y`, sixteen bits a
/// coordinate. Sixteen bits reaches a frame 1,048,576 pixels wide.
uvec2 packSpriteRect(uvec2 from, uvec2 to)
{
    return uvec2(from.x | (from.y << 16u), to.x | (to.y << 16u));
}

void unpackSpriteRect(uvec2 packed, out uvec2 from, out uvec2 to)
{
    from = uvec2(packed.x & 0xFFFFu, packed.x >> 16u);
    to = uvec2(packed.y & 0xFFFFu, packed.y >> 16u);
}

#endif
