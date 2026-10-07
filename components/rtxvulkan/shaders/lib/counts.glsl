#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_COUNTS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_COUNTS_GLSL

// The trace's own counts, `FrameCounts` in `counts.h`, bound by each launch on its own buffer, so a
// picture's misses are not the world's. The hold's own clock in `stress.comp` is not a count and is
// not here: it binds the block on a layout of its own, because it runs on nothing else of the
// frame's. The stores that were not finite are the device's census (`census.glsl`), apart.

#include "bindings.glsl"
#include "variants.glsl"

/// Counts one primary ray that reached nothing, from the miss shader. `FrameCounts::mMisses` says
/// why the misses are counted and the hits derived, and `COUNTING` why the atomic is not in the
/// game's kernel at all.
void countMiss()
{
    if (COUNTING)
        atomicAdd(counts.mMisses, 1u);
}

#endif
