#pragma once

#include <algorithm>
#include <cstdint>

#include <volk.h>

#include <components/rtx/shaders/skinning.h>
#include <components/rtxvulkan/shaders/shared/probe.h>
#include <components/rtxvulkan/shaders/shared/tables.h>

namespace Rtx
{
    /// The most any shader's `buffer_reference_align` claims of where a buffer it reads by address starts, which
    /// every such buffer's memory starts on (`Buffer`). Every claim a shader makes names one of these, and
    /// `RtxSpirvInterfaceTest` holds every access the shipped modules make by address to it, so a claim written
    /// with a constant not named here fails there.
    inline constexpr VkDeviceSize sReferenceAlignment = std::max({ Shaders::TABLE_ALIGN_ROWS,
        Shaders::TABLE_ALIGN_MATERIALS, Shaders::TABLE_ALIGN_BLOCKS, Shaders::TABLE_ALIGN_LAYERS,
        Shaders::TABLE_ALIGN_INSTANCES, Shaders::BONE_ALIGN, static_cast<std::uint32_t>(Shaders::PROBE_ROW_ALIGN) });
}
