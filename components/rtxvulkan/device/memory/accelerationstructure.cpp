#include "accelerationstructure.hpp"

#include <bit>
#include <cassert>
#include <cstdint>
#include <utility>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/graveyard.hpp>
#include <components/rtxvulkan/device/result.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

#include "buffer.hpp"

namespace Rtx
{
    AccelerationStructure::AccelerationStructure(const Device& device, const VkAccelerationStructureTypeKHR type,
        const VkBuffer buffer, const VkDeviceSize offset, const VkDeviceSize size, StructureStorage* const storage,
        const StructureRoom& room)
        : mDevice(&device)
        , mStorage(storage)
        , mRoom(room)
    {
        const DeviceFunctions& functions = device.getFunctions();

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
        checkVk(functions.mCreateAccelerationStructure(device.getHandle(), &create, nullptr, &mHandle),
            "vkCreateAccelerationStructureKHR");

        // Asked before anything is built into it: an address belongs to the structure from the
        // moment it is created, and what a barrier orders is the contents arriving.
        const VkAccelerationStructureDeviceAddressInfoKHR address{
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
            .pNext = nullptr,
            .accelerationStructure = mHandle,
        };
        mAddress = functions.mGetAccelerationStructureDeviceAddress(device.getHandle(), &address);
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
        device.setName(made.mHandle, name);
        return made;
    }

    AccelerationStructure::~AccelerationStructure()
    {
        reset();
    }

    AccelerationStructure::AccelerationStructure(AccelerationStructure&& other) noexcept
        : mDevice(other.mDevice)
        , mHandle(std::exchange(other.mHandle, VK_NULL_HANDLE))
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
            mHandle = std::exchange(other.mHandle, VK_NULL_HANDLE);
            mAddress = std::exchange(other.mAddress, 0);
            mStorage = other.mStorage;
            mRoom = std::exchange(other.mRoom, StructureRoom{});
        }

        return *this;
    }

    void AccelerationStructure::reset()
    {
        if (mHandle != VK_NULL_HANDLE)
        {
            // Under one stamp, the handle ended first: the room is the next structure's once the
            // timeline passes it, and one handed out under a structure still standing would be two
            // of them in one place.
            mDevice->getGraveyard().bury(&end, std::bit_cast<std::uint64_t>(mHandle), DeviceMemory());
            if (mStorage != nullptr)
                mStorage->retire(mRoom, mDevice->getTimeline().getNext());
        }

        mHandle = VK_NULL_HANDLE;
        mAddress = 0;
        mRoom = StructureRoom{};
    }

    void AccelerationStructure::end(const Device& device, const std::uint64_t handle)
    {
        device.getFunctions().mDestroyAccelerationStructure(
            device.getHandle(), std::bit_cast<VkAccelerationStructureKHR>(handle), nullptr);
    }
}
