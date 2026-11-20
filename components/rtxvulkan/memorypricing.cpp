#include "memorypricing.hpp"

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/renderer/memoryprices.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/bufferusage.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/scene/bottomlevelstore.hpp>
#include <components/rtxvulkan/scene/structurebuild.hpp>

namespace Rtx
{
    MemoryPrices priceMemory(const Device& device)
    {
        const VkPhysicalDevice card = device.getPhysicalDevice().getHandle();
        MemoryPrices prices;

        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(card, &memory);
        for (std::uint32_t type = 0; type < memory.memoryTypeCount; ++type)
        {
            const VkMemoryType& kind = memory.memoryTypes[type];
            prices.mKinds.push_back(MemoryKind{
                .mHeap = kind.heapIndex,
                .mHeapBytes = memory.memoryHeaps[kind.heapIndex].size,
                .mOnDevice = (kind.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0,
                .mHostVisible = (kind.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0,
            });
        }

        VkPhysicalDeviceProperties stated{};
        vkGetPhysicalDeviceProperties(card, &stated);
        prices.mAllocationsMost = stated.limits.maxMemoryAllocationCount;
        prices.mGranularity = stated.limits.bufferImageGranularity;
        prices.mStructureKinds
            = Buffer::requirementsFor(device, sContentBlockBytes, sStructureStorageUsage).memoryTypeBits;

        const std::uint32_t sideMost = device.getPhysicalDevice().getTextureSide();
        for (const TextureFormat format : sPricedFormats)
            for (std::uint32_t side = 1; side <= sideMost; side *= 2)
            {
                const VkMemoryRequirements asked = Image::requirementsFor(device,
                    ImageDescription{ .mWidth = side,
                        .mHeight = side,
                        .mFormat = toVulkanFormat(format),
                        .mUsage = sUploadedTextureUsage,
                        .mMipLevels = chainLevels(side) });
                prices.mTextures.push_back(TexturePrice{ .mFormat = format,
                    .mSide = side,
                    .mLevels = chainLevels(side),
                    .mTexelBytes = chainTexelBytes(format, side),
                    .mBytes = asked.size,
                    .mAlignment = asked.alignment,
                    .mKinds = asked.memoryTypeBits });
            }

        for (const std::uint32_t triangles : sPricedTriangles)
        {
            // Two vertices and one more a triangle, as a strip shares them: a mesh's corners are shared by about two
            // triangles each, and the driver sizes by the triangles.
            MeshRange mesh;
            mesh.mVertices.mCount = triangles + 2;
            mesh.mIndices.mCount = 3 * triangles;
            const VkAccelerationStructureGeometryKHR geometry = describeTriangles(mesh, 0, 0);
            const VkAccelerationStructureBuildGeometryInfoKHR build{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                .pNext = nullptr,
                .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                .flags = BottomLevelStore::buildFlagsOf(StructureMotion::Still),
                .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                .srcAccelerationStructure = VK_NULL_HANDLE,
                .dstAccelerationStructure = VK_NULL_HANDLE,
                .geometryCount = 1,
                .pGeometries = &geometry,
                .ppGeometries = nullptr,
                .scratchData = {},
            };
            VkAccelerationStructureBuildSizesInfoKHR sizes{
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
                .pNext = nullptr,
                .accelerationStructureSize = 0,
                .updateScratchSize = 0,
                .buildScratchSize = 0,
            };
            vkGetAccelerationStructureBuildSizesKHR(
                device.getHandle(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &triangles, &sizes);
            prices.mStructures.push_back(StructurePrice{ .mTriangles = triangles,
                .mBytes = sizes.accelerationStructureSize,
                .mScratchBytes = sizes.buildScratchSize });
        }
        return prices;
    }
}
