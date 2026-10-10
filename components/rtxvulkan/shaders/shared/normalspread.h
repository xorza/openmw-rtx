#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_NORMALSPREAD_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_NORMALSPREAD_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

// A normal map's spread, measured on the device as it arrives: what `normalspread.comp` is told
// about the level it writes. `Rtx::NormalSpreadPass` says what the spread is and why.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `normalspread.comp` binds what it reads and writes in set 0, and how many there are.
    const uint NORMALSPREAD_BIND_SOURCE = 0;
    const uint NORMALSPREAD_BIND_MEANS = 1;
    const uint NORMALSPREAD_BIND_SPREAD = 2;
    const uint NORMALSPREAD_BINDINGS = 3;

    /// The pass's workgroup, square.
    const uint NORMAL_SPREAD_WORKGROUP = 16u;

    /// One level of the spread, which is one dispatch.
    struct NormalSpreadConstants
    {
        /// Which of the normal map's levels the spread is written for, from one: the first averages
        /// the map's own texels, and every other the means of the one before.
        uint mLevel;

        uint mWidth;
        uint mHeight;

        /// The extent of the level before: the map's own where the level written is the first.
        uint mAboveWidth;
        uint mAboveHeight;

        /// Where in the means, in texels, the level before's start and the level written's, a level
        /// row by row. The first reads the map and none of the means.
        uint mAboveAt;
        uint mMeanAt;
    };

#ifdef RTX_HOST
    static_assert(sizeof(NormalSpreadConstants) == 28, "NormalSpreadConstants must be scalar-packed on every side");
}
#endif

/// What a normal map's spread is stored as: the roughness a level loses, a byte, which is the
/// precision the specular map it widens paints its own roughness at.
#define NORMAL_SPREAD_FORMAT STORAGE_R8

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The bytes of one texel of the means the chain is built through, a level at a time, with the
    /// loss beside them in `a`: four whole floats, because the loss is made of the differences between
    /// means that are all near one long, which a half resolves no finer than five parts in ten
    /// thousand.
    const uint NORMAL_SPREAD_MEAN_BYTES = 16u;

#ifdef RTX_HOST
}
#endif

#endif
