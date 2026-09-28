#include "ngxdispatch.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace Rtx
{
    namespace
    {
        /// NGX's `vkCmdPipelineBarrier`: the call it recorded, with `orderTransferWrites` applied to
        /// each image barrier, handed on to the loader.
        VKAPI_ATTR void VKAPI_CALL barrierForNgx(VkCommandBuffer commands, VkPipelineStageFlags sources,
            VkPipelineStageFlags destinations, VkDependencyFlags dependencies, std::uint32_t memoryCount,
            const VkMemoryBarrier* memory, std::uint32_t bufferCount, const VkBufferMemoryBarrier* buffers,
            std::uint32_t imageCount, const VkImageMemoryBarrier* images)
        {
            // The recording thread's own, because NGX records on whichever thread evaluates, and
            // refilled per call: a barrier holds a handful of images, so this allocates once.
            thread_local std::vector<VkImageMemoryBarrier> widened;
            widened.assign(images, images + imageCount);

            bool transfers = false;
            for (VkImageMemoryBarrier& barrier : widened)
                transfers = orderTransferWrites(barrier) || transfers;

            vkCmdPipelineBarrier(commands, sources,
                transfers ? destinations | VK_PIPELINE_STAGE_TRANSFER_BIT : destinations, dependencies, memoryCount,
                memory, bufferCount, buffers, imageCount, widened.data());
        }

        /// The one name this answers for itself, or null for every other.
        PFN_vkVoidFunction corrected(const std::string_view name)
        {
            if (name == "vkCmdPipelineBarrier")
                return reinterpret_cast<PFN_vkVoidFunction>(&barrierForNgx);

            return nullptr;
        }
    }

    VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ngxInstanceProcAddr(VkInstance instance, const char* name)
    {
        // The two lookups as well, so no path NGX takes to a function leads round the correction.
        const std::string_view asked(name);
        if (asked == "vkGetInstanceProcAddr")
            return reinterpret_cast<PFN_vkVoidFunction>(&ngxInstanceProcAddr);
        if (asked == "vkGetDeviceProcAddr")
            return reinterpret_cast<PFN_vkVoidFunction>(&ngxDeviceProcAddr);
        if (const PFN_vkVoidFunction own = corrected(asked))
            return own;

        return vkGetInstanceProcAddr(instance, name);
    }

    VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL ngxDeviceProcAddr(VkDevice device, const char* name)
    {
        if (const PFN_vkVoidFunction own = corrected(name))
            return own;

        return vkGetDeviceProcAddr(device, name);
    }

    bool orderTransferWrites(VkImageMemoryBarrier& barrier)
    {
        const bool transferWritable
            = barrier.newLayout == VK_IMAGE_LAYOUT_GENERAL || barrier.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        if (barrier.oldLayout == barrier.newLayout || !transferWritable)
            return false;

        barrier.dstAccessMask |= VK_ACCESS_TRANSFER_WRITE_BIT;
        return true;
    }
}
