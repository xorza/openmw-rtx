#pragma once

#include <components/rtx/renderer/memoryprices.hpp>

namespace Rtx
{
    class Device;

    /// What `device` charges for what content stands in (`MemoryPrices`): each texture as `Texture::describeFile`
    /// describes a file's, each structure as `BottomLevelStore` first builds a mesh that does not deform, and the
    /// buffer a structure storage's block is. Asked of the driver alone: no image, buffer or structure is made.
    MemoryPrices priceMemory(const Device& device);
}
