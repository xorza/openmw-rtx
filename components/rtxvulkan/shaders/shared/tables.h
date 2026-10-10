#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_TABLES_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_TABLES_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// How the shaders reach the scene's tables: by address, each through a `buffer_reference` whose
// alignment is claimed here, for the shader that declares it and the host that checks the claim.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// What a reference to each table may claim about its address, and so what the host checks.
    ///
    /// **The largest power of two that divides both the buffer's start and every element access.**
    /// A claim larger than the truth is undefined behaviour with no message. A claim smaller than
    /// the truth costs the compiler a wider load where one was possible. A buffer's start is at
    /// least sixteen-aligned on this device and the host asserts it, so the stride decides:
    /// `GpuLayer` is 64 bytes with two `vec4` at sixteen and thirty-two, the block tables hold
    /// eight-byte addresses, a top level's instance row is 64 bytes moved as four sixteen-byte words
    /// (and sixteen-aligned by Vulkan's own rule for instance data), `GpuMaterial` is 112 bytes so
    /// that what the candidate loop reads of a row lies in one sector, and every other row or list
    /// is four-aligned only.
    const uint TABLE_ALIGN_ROWS = 4u;
    const uint TABLE_ALIGN_MATERIALS = 16u;
    const uint TABLE_ALIGN_BLOCKS = 8u;
    const uint TABLE_ALIGN_LAYERS = 16u;
    const uint TABLE_ALIGN_INSTANCES = 16u;

#ifdef RTX_HOST
}
#endif

#endif
