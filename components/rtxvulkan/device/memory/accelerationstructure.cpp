#include "accelerationstructure.hpp"

#include <utility>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/result.hpp>

#include "buffer.hpp"

namespace Rtx
{
    AccelerationStructure::AccelerationStructure(const Device& device, const VkAccelerationStructureTypeKHR type,
        const VkBuffer buffer, const VkDeviceSize offset, const VkDeviceSize size)
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
        const Device& device, const VkBuffer buffer, const VkDeviceSize offset, const VkDeviceSize size)
    {
        return AccelerationStructure(device, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, buffer, offset, size);
    }

    AccelerationStructure AccelerationStructure::topLevel(
        const Device& device, const Buffer& storage, const VkDeviceSize size, const std::string_view name)
    {
        AccelerationStructure made(device, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, storage.getHandle(), 0, size);
        device.setName(made.mHandle.get(), name);
        return made;
    }

    AccelerationStructure::AccelerationStructure(AccelerationStructure&& other) noexcept
        : mHandle(std::move(other.mHandle))
        , mAddress(std::exchange(other.mAddress, 0))
    {
    }

    AccelerationStructure& AccelerationStructure::operator=(AccelerationStructure&& other) noexcept
    {
        if (this != &other)
        {
            mHandle = std::move(other.mHandle);
            mAddress = std::exchange(other.mAddress, 0);
        }
        return *this;
    }
}
