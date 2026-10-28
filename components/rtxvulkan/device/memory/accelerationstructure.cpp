#include "accelerationstructure.hpp"

#include <cassert>
#include <cstdint>
#include <utility>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/result.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

#include "buffer.hpp"
#include "memory.hpp"

namespace Rtx
{
    AccelerationStructure::AccelerationStructure(const Device& device, const VkAccelerationStructureTypeKHR type,
        const VkBuffer buffer, const VkDeviceSize offset, const VkDeviceSize size, StructureStorage* const storage,
        const StructureRoom& room)
        : mDevice(&device)
        , mStorage(storage)
        , mRoom(room)
    {
        const VkAccelerationStructureCreateInfoKHR create{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .pNext = nullptr,
            .createFlags = 0,
            .buffer = buffer,
            .offset = offset,
            .size = size,
            .type = type,
            .deviceAddress = 0,
        };
        mHandle = Owned<VkAccelerationStructureKHR, vkDestroyAccelerationStructureKHR>::make(
            device, vkCreateAccelerationStructureKHR, create, "vkCreateAccelerationStructureKHR");

        // Asked before anything is built into it: an address belongs to the structure from the
        // moment it is created, and what a barrier orders is the contents arriving.
        const VkAccelerationStructureDeviceAddressInfoKHR address{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
            .pNext = nullptr,
            .accelerationStructure = mHandle.get(),
        };
        mAddress = vkGetAccelerationStructureDeviceAddressKHR(device.getHandle(), &address);
    }

    AccelerationStructure AccelerationStructure::bottomLevel(
        const Device& device, StructureStorage& storage, const StructureRoom& room, const VkDeviceSize size)
    {
        return AccelerationStructure(device, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, storage.getBuffer(room),
            storage.getOffset(room), size, &storage, room);
    }

    AccelerationStructure AccelerationStructure::topLevel(
        const Device& device, const Buffer& storage, const VkDeviceSize size, const std::string_view name)
    {
        AccelerationStructure made(
            device, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, storage.getHandle(), 0, size, nullptr, {});
        device.setName(made.mHandle.get(), name);
        return made;
    }

    AccelerationStructure::~AccelerationStructure()
    {
        reset();
    }

    AccelerationStructure::AccelerationStructure(AccelerationStructure&& other) noexcept
        : mDevice(other.mDevice)
        , mHandle(std::move(other.mHandle))
        , mAddress(std::exchange(other.mAddress, 0))
        , mStorage(other.mStorage)
        , mRoom(std::exchange(other.mRoom, StructureRoom{}))
    {
    }

    AccelerationStructure& AccelerationStructure::operator=(AccelerationStructure&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            mDevice = other.mDevice;
            mHandle = std::move(other.mHandle);
            mAddress = std::exchange(other.mAddress, 0);
            mStorage = other.mStorage;
            mRoom = std::exchange(other.mRoom, StructureRoom{});
        }

        return *this;
    }

    void AccelerationStructure::reset()
    {
        if (!isEmpty())
        {
            // Under one stamp, the handle ended first: the room is the next structure's once the
            // timeline passes it, and one handed out under a structure still standing would be two
            // of them in one place.
            mHandle.reset();
            if (mStorage != nullptr)
                mStorage->retire(mRoom, mDevice->getTimeline().getNext());
        }

        mAddress = 0;
        mRoom = StructureRoom{};
    }
}
