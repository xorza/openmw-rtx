#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SPRITELIGHT_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SPRITELIGHT_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// The sprite light bake's dispatch: what `spritelight.comp` is told about the level it bakes.
// `Rtx::SpriteLightMap` says what the bake is and why.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `spritelight.comp` binds what it reads and writes in set 0, and how many there are.
    /// The shader's layout and the pass's own layout and writes are numbered by these and by
    /// nothing else, so the two cannot drift apart.
    const uint SPRITE_LIGHT_BIND_SOURCE = 0;
    const uint SPRITE_LIGHT_BIND_BAKE = 1;
    const uint SPRITE_LIGHT_BINDINGS = 2;

    /// Lanes in one workgroup, a line of texels each, which the kernel declares and the pass
    /// divides a level's rows or columns by.
    const uint SPRITE_LIGHT_WORKGROUP = 64u;

    /// The four stages of a level's bake, in the order they run, each the channel it writes: light
    /// from `+u` along every row, from `-u`, from `+v` down every column, and from `-v`.
    const uint SPRITE_LIGHT_FROM_RIGHT = 0u;
    const uint SPRITE_LIGHT_FROM_LEFT = 1u;
    const uint SPRITE_LIGHT_FROM_BELOW = 2u;
    const uint SPRITE_LIGHT_FROM_ABOVE = 3u;
    const uint SPRITE_LIGHT_STAGES = 4u;

    /// One stage of one level of the bake, which is one dispatch.
    struct SpriteLightConstants
    {
        /// Which level of the source is read and which of the bake is written.
        uint mLevel;

        uint mWidth;
        uint mHeight;

        /// Which channel this dispatch runs, `SPRITE_LIGHT_FROM_*`.
        uint mStage;
    };

#ifdef RTX_HOST
    static_assert(sizeof(SpriteLightConstants) == 16, "SpriteLightConstants must be scalar-packed on every side");
}
#endif

#endif
