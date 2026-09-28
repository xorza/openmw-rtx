#ifndef OPENMW_COMPONENTS_RTX_SHADERS_NORMALSPREAD_H
#define OPENMW_COMPONENTS_RTX_SHADERS_NORMALSPREAD_H

#include "hosttypes.h"
#include "portable.h"
#include "storageformat.h"

// A normal map's spread, measured on the device as it arrives: what `normalspread.comp` is told
// about the level it writes. `Rtx::NormalSpreadPass` says what the spread is and why.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `normalspread.comp` binds what it reads and writes in set 0, and how many there are.
    const uint NORMALSPREAD_BIND_SOURCE = 0;
    const uint NORMALSPREAD_BIND_ABOVE = 1;
    const uint NORMALSPREAD_BIND_MEAN = 2;
    const uint NORMALSPREAD_BIND_SPREAD = 3;
    const uint NORMALSPREAD_BINDINGS = 4;

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

        uint mPadding;
    };

#ifdef RTX_HOST
    static_assert(sizeof(NormalSpreadConstants) == 16, "NormalSpreadConstants must be scalar-packed on every side");
}
#endif

/// What a normal map's spread is stored as: the roughness a level loses, a byte, which is the
/// precision the specular map it widens paints its own roughness at.
#define NORMAL_SPREAD_FORMAT STORAGE_R8

/// The means the chain is built through, a level at a time, with the loss beside them in `a`:
/// whole floats, because the loss is made of the differences between means that are all near one
/// long, which a half resolves no finer than five parts in ten thousand.
#define NORMAL_SPREAD_MEAN_FORMAT STORAGE_RGBA32F

#endif
