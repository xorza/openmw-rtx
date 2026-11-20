#include "memorypricing.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/renderer/memoryprices.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/bufferusage.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/result.hpp>
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

    std::vector<CopyRate> measureCopies(Device& device)
    {
        const VkPhysicalDeviceLimits& limits
            = device.getPhysicalDevice().getProperties().mProperties2.properties.limits;
        const std::uint32_t bits = device.getPhysicalDevice().getTimestampBits();
        std::vector<CopyRate> rates;
        if (bits == 0 || limits.timestampPeriod <= 0.0f)
            return rates;
        const std::uint64_t mask = bits >= 64 ? ~std::uint64_t{ 0 } : (std::uint64_t{ 1 } << bits) - 1;

        const VkQueryPoolCreateInfo timed{
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = 2,
            .pipelineStatistics = 0,
        };
        const QueryPool clock = QueryPool::make(device, vkCreateQueryPool, timed, "vkCreateQueryPool");

        for (const std::uint32_t side : sCopiedSides)
        {
            const std::uint32_t levels = chainLevels(side);
            const ImageDescription chain{ .mWidth = side,
                .mHeight = side,
                .mFormat = toVulkanFormat(TextureFormat::Bc7Srgb),
                .mUsage = sUploadedTextureUsage,
                .mMipLevels = levels };
            const std::uint64_t chainBytes = chainTexelBytes(TextureFormat::Bc7Srgb, side);
            // Four at least, so a side whose chain is most of the run is still more than one copy.
            const auto copies
                = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(sCopiedBytes / chainBytes, 4, 2048));

            const Image source(MemoryUse::Essential, device, chain, "copied chain");
            std::vector<Image> into;
            into.reserve(copies);
            for (std::uint32_t at = 0; at < copies; ++at)
                into.emplace_back(MemoryUse::Essential, device, chain, "copy of a chain");

            std::vector<VkImageCopy> regions;
            regions.reserve(levels);
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                const std::uint32_t texels = std::max(side >> level, 1u);
                regions.push_back(VkImageCopy{ .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 },
                    .srcOffset = {},
                    .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 },
                    .dstOffset = {},
                    .extent = { texels, texels, 1 } });
            }

            device.getPool().submitAndWait([&](const VkCommandBuffer commands) {
                std::vector<VkImageMemoryBarrier2> room(copies + 1);
                {
                    Barriers barriers(commands, room);
                    source.addTransition(barriers, Use::sUndefined, Use::sCopyRead);
                    for (const Image& image : into)
                        image.addTransition(barriers, Use::sUndefined, Use::sCopyWrite);
                    barriers.flush();
                }
                vkCmdResetQueryPool(commands, clock.get(), 0, 2);
                vkCmdWriteTimestamp2(commands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, clock.get(), 0);
                for (const Image& image : into)
                    vkCmdCopyImage(commands, source.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        image.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels, regions.data());
                vkCmdWriteTimestamp2(commands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, clock.get(), 1);
            });

            std::array<std::uint64_t, 2> stamps{};
            checkVk(device,
                vkGetQueryPoolResults(device.getHandle(), clock.get(), 0, 2, sizeof(stamps), stamps.data(),
                    sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                "vkGetQueryPoolResults");
            const std::uint64_t ticks = ((stamps[1] & mask) - (stamps[0] & mask)) & mask;
            rates.push_back(CopyRate{ .mSide = side,
                .mCopies = copies,
                .mBytes = chainBytes * copies,
                .mMs = static_cast<double>(ticks) * static_cast<double>(limits.timestampPeriod) / 1.0e6 });
        }
        return rates;
    }
}
