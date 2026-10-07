#ifndef OPENMW_COMPONENTS_RTX_SHADERS_SHADINGMAP_H
#define OPENMW_COMPONENTS_RTX_SHADERS_SHADINGMAP_H

#include "hosttypes.h"
#include "look.h"
#include "portable.h"
#include "storageformat.h"

// The shading estimate's dispatch: what `shadingmap.comp` is told about the texture it reads,
// and the one number the estimate is made with that both the dispatch and `Rtx::ShadingMap`
// have to agree on. `Rtx::ShadingMap` says what the estimate is and why.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `shadingsum.comp` binds what it reads and writes in set 0, and how many there are. The
    /// shader's layout and the pass's own layout and writes are numbered by these and by nothing
    /// else, so the two cannot drift apart.
    const uint SHADING_SUM_BIND_SOURCE = 0;
    const uint SHADING_SUM_BIND_SUMS = 1;
    const uint SHADING_SUM_BINDINGS = 2;

    /// Where `shadingmap.comp` binds what it reads and writes in set 0, and how many there are. The
    /// shader's layout and the pass's own layout and writes are numbered by these and by nothing
    /// else, so the two cannot drift apart.
    const uint SHADING_MAP_BIND_SUMS = 0;
    const uint SHADING_MAP_BIND_MAP = 1;
    const uint SHADING_MAP_BINDINGS = 2;

/// What the map is stored as, which the dispatch stores and the trace samples: the unorm
/// `encodeShading` rounds to.
#define SHADING_MAP_FORMAT STORAGE_R16

    /// How many times the grid is box blurred, three by three, before it is normalised: three passes
    /// are a close enough Gaussian for anything this coarse, and a correction with an edge in it
    /// would put that edge into the frame. **Wrapping along an axis the texture repeats**, because
    /// Morrowind's textures tile and a blur that clamped there would invent a gradient across every
    /// wall; **and clamped along one it clamps** (`ShadingConstants::mWrap`), because that is an
    /// unwrapped picture — a banner, a flame — whose border cells took the opposite edge's light and
    /// divided real albedo by it.
    const uint SHADING_BLUR_PASSES = 3u;

    /// One factor as the map's format stores it, before the unorm's rounding: its place between
    /// the floor and the ceiling, so the neutral factor is exactly a third and a step is a part in
    /// forty thousand. `look.h` says why the range is the map's and not the format's. The one
    /// statement of the encode, which the dispatch stores and `Rtx::encodeShading` rounds.
    RTX_SHADER float shadingUnit(float factor)
    {
        const float unit = (factor - SHADING_FLOOR) / (SHADING_CEILING - SHADING_FLOOR);
        return unit < 0.0f ? 0.0f : (unit > 1.0f ? 1.0f : unit);
    }

    /// The factor a place between the floor and the ceiling stands for: `shadingUnit` undone, which
    /// the trace applies to a filtered read and `Rtx::decodeShading` to a stored one.
    ///
    /// **An explicit `fma`, so both sides round it once and at the same place.** The shader's
    /// `mix` is pinned to this very form (`Rtx::pinFloatArithmetic`), where the host's obvious
    /// `floor + span * unit` rounds twice elsewhere; a map decoded two ways reads two lights.
    RTX_SHADER float shadingFactor(float unit)
    {
        return fma(SHADING_CEILING, unit, SHADING_FLOOR * (1.0f - unit));
    }

    /// One cell's sum, as the summing stage leaves it for the map stage: the linear luminance of
    /// its texels weighed by their alpha, and the alpha summed. **Weighed by alpha in every format**:
    /// a texel nothing was painted on is usually black, and counted whole it darkened every cell
    /// round a leaf's holes, which the delighting then brightened the leaves against. A format
    /// with no alpha weighs every texel one, and BC1's holes are an alpha of nought like any other.
    struct ShadingSum
    {
        float mSum;
        float mWeight;
    };

#ifdef RTX_HOST
    static_assert(sizeof(ShadingSum) == 8, "ShadingSum must be scalar-packed on every side");
}
#endif

#endif
