#pragma once

#include <cstdint>
#include <string_view>

#include <volk.h>

#include <components/rtxvulkan/device/owned.hpp>

#include "structurestorage.hpp"

namespace Rtx
{
    class Buffer;
    class Device;

    /// A `VkAccelerationStructureKHR`, its device address, and the room it stands in. Made once
    /// with its address asked once — a handle lasts until the mesh it belongs to is released, and
    /// the alternative was the same question per instance per frame. Destroying one buries the
    /// handle and cools the room under the same stamp, so a frame still tracing it traces it to
    /// the end, and two structures never stand in one place.
    class AccelerationStructure
    {
    public:
        /// A slot with nothing in it: a mesh the scene took back and has not filled.
        AccelerationStructure() = default;

        /// A bottom level standing in `room` of `storage`, which is given back when this goes.
        static AccelerationStructure bottomLevel(
            const Device& device, StructureStorage& storage, const StructureRoom& room, VkDeviceSize size);

        /// A top level at the start of `storage`, which is the caller's to keep.
        static AccelerationStructure topLevel(
            const Device& device, const Buffer& storage, VkDeviceSize size, std::string_view name);

        ~AccelerationStructure();

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
            VkDeviceSize offset, VkDeviceSize size, StructureStorage* storage, const StructureRoom& room);

        /// Buries the handle and cools the room, where there is a handle.
        void reset();

        const Device* mDevice = nullptr;
        Owned<VkAccelerationStructureKHR, vkDestroyAccelerationStructureKHR> mHandle;
        VkDeviceAddress mAddress = 0;

        /// Null for a top level, whose storage is a buffer of its own.
        StructureStorage* mStorage = nullptr;
        StructureRoom mRoom;
    };
}
