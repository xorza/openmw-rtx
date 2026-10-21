#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_TRACECONSTANTS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_TRACECONSTANTS_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

#include "fields.h"

// What the trace's kernels are built from beside their shaders: the specialization constants, the
// workgroup and its tile lists, the bound on a walk, and what an instance's shader-table offset
// says. Included verbatim by both sides, for the reason `visibility.h` is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The trace's specialization constants, by `constant_id`: the frame's tuple, which every kernel
    /// of the trace is handed. `lib/variants.glsl` declares them by these names, and the host
    /// fills one table indexed by them, so no table's order has to agree with a declaration's.
    const uint SPEC_COUNTING = 0u;
    const uint SPEC_HAS_SUN = 1u;
    const uint SPEC_HAS_MOONS = 2u;
    const uint SPEC_HAS_SEA = 3u;
    const uint SPEC_HAS_MAPS = 4u;
    const uint SPEC_COUNT = 5u;

    /// The columns and rows of a field one workgroup of a trace stage covers (`stagePixelOf`): 32
    /// lanes, one subgroup (`SubgroupSize::ThirtyTwo`), in AMD's eight by four for ray queries on
    /// RDNA, which keeps a wave's rays near each other.
    const uint TRACE_WORKGROUP_WIDTH = 8u;
    const uint TRACE_WORKGROUP_HEIGHT = 4u;
    const uint TRACE_WORKGROUP_LANES = TRACE_WORKGROUP_WIDTH * TRACE_WORKGROUP_HEIGHT;

    // A word holds a bit a lane of a trace workgroup, which the bounce's stage lists its landings by
    // (`bounce.comp`).
#ifdef RTX_HOST
    static_assert(TRACE_WORKGROUP_LANES <= 32u, "a trace workgroup wider than the bounce's landing mask");
#endif

    /// How many tiles each of the stages' lists holds for a frame `width` by `height`: every
    /// workgroup a stage's dispatch over the stored layout runs (`stagePixelIn`). One statement for
    /// the host, which sizes the lists, and the shaders, which index them.
    RTX_SHADER uint stageTileCount(uint width, uint height)
    {
        return 2u * fieldGroupsAcross(width, TRACE_WORKGROUP_WIDTH)
            * ((height + TRACE_WORKGROUP_HEIGHT - 1u) / TRACE_WORKGROUP_HEIGHT);
    }

    /// The most turns of a trace stage's loops a pixel's walk takes before it is cut short
    /// (`FrameCounts::mWalksCut`): four times the deepest walk counted by hand — water seen through
    /// a full peel, each layer's column marched and both of the water's legs under it — which
    /// comes to about five hundred.
    const uint WALK_TURNS = 2048u;

    /// What an instance's shader-table offset holds: its material's kind, `Rtx::MaterialKind`, which
    /// `SceneAcceleration::placeRow` writes and the trace reads back off a committed hit
    /// (`SceneAnswer::mKind`). **No shader table stands behind it**: the visibility trace is ray
    /// queries alone (`visibility.comp`), and the offset is the field of an instance traversal hands
    /// back that nothing else takes.
    const uint KIND_SURFACE = 0u;
    const uint KIND_TERRAIN = 1u;
    const uint KIND_WATER = 2u;

#ifdef RTX_HOST
}
#endif

#endif
