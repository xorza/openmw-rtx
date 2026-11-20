#pragma once

#include <cstdint>
#include <string_view>

#include <volk.h>

#include <components/rtxvulkan/device/owned.hpp>

namespace Rtx
{
    class Buffer;
    class Device;

    /// A `VkAccelerationStructureKHR` and its device address, made once with its address asked once — a handle lasts
    /// until the mesh it belongs to is released, and the alternative was the same question per instance per frame.
    /// Destroying one buries the handle, so a frame still tracing it traces it to the end; the range it stood in is
    /// its owner's, which hands it out again only once those frames are done (a slot's epoch).
    class AccelerationStructure
    {
    public:
        /// A slot with nothing in it: a mesh the scene took back and has not filled.
        AccelerationStructure() = default;

        /// A bottom level of `size` at `offset` of `buffer`, which the caller keeps for as long as this stands.
        static AccelerationStructure bottomLevel(
            const Device& device, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);

        /// A top level at the start of `storage`, which is the caller's to keep.
        static AccelerationStructure topLevel(
            const Device& device, const Buffer& storage, VkDeviceSize size, std::string_view name);

        ~AccelerationStructure() = default;

        AccelerationStructure(const AccelerationStructure&) = delete;
        AccelerationStructure& operator=(const AccelerationStructure&) = delete;
        AccelerationStructure(AccelerationStructure&& other) noexcept;
        AccelerationStructure& operator=(AccelerationStructure&& other) noexcept;

        /// Null for an empty slot.
        VkAccelerationStructureKHR getHandle() const { return mHandle.get(); }
        VkDeviceAddress getAddress() const { return mAddress; }
        bool isEmpty() const { return mHandle.get() == VK_NULL_HANDLE; }

    private:
        AccelerationStructure(const Device& device, VkAccelerationStructureTypeKHR type, VkBuffer buffer,
            VkDeviceSize offset, VkDeviceSize size);

        Owned<VkAccelerationStructureKHR, vkDestroyAccelerationStructureKHR> mHandle;
        VkDeviceAddress mAddress = 0;
    };
}
