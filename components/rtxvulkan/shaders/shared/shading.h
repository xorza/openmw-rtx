#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SHADING_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SHADING_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What `shadingsum.comp` and `shadingmap.comp` are told about the texture they read. `shadingmap.h`
// says what the map is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// What the dispatch is told about the texture's finest level.
    struct ShadingConstants
    {
        uint mWidth;
        uint mHeight;

        /// How the texture is addressed past its edges, `Rtx::TextureWrap`: bit nought clamps the
        /// blur across, bit one down.
        uint mWrap;
    };

#ifdef RTX_HOST
    static_assert(sizeof(ShadingConstants) == 12, "ShadingConstants must be scalar-packed on every side");
}
#endif

#endif
