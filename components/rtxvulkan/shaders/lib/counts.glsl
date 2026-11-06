#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_COUNTS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_COUNTS_GLSL

// The trace's own counts, `FrameCounts` in `counts.h`, bound by each trace on its own buffer, so a
// picture's misses are not the world's.

#include "bindings.glsl"
#include "variants.glsl"

/// Counts one primary ray that reached nothing, where the kernel draws its sky (`missedAlong`).
/// `FrameCounts::mMisses` says why the misses are counted and the hits derived, and `COUNTING` why
/// the atomic is not in the game's kernel at all.
void countMiss()
{
    if (COUNTING)
        atomicAdd(counts.mMisses, 1u);
}

#endif
