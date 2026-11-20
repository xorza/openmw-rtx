#pragma once

#include <vector>

#include <components/rtx/renderer/memoryprices.hpp>

namespace Rtx
{
    class Device;

    /// What `device` charges for what content stands in (`MemoryPrices`): each texture as `Texture::describeFile`
    /// describes a file's, each structure as `BottomLevelStore` first builds a mesh that does not deform, and the
    /// buffer a structure storage's block is. Asked of the driver alone: no image, buffer or structure is made.
    MemoryPrices priceMemory(const Device& device);

    /// How fast `device` copies BC7 chains of each of `sCopiedSides` into images of their own (`CopyRate`), timed by
    /// its own clock around the copies alone; empty where its queue keeps no time.
    std::vector<CopyRate> measureCopies(Device& device);
}
