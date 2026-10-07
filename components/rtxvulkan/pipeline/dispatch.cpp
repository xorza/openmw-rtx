#include "dispatch.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <span>

#include <components/rtxvulkan/device/notfinitecensus.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>

namespace Rtx
{
    void pushDescriptors(const VkCommandBuffer commands, const Pipeline& pipeline, const DescriptorWrites& writes)
    {
        assert(writes.against(pipeline.getBindings()) && "writes made against another layout than the one pushed to");

        const std::span<const VkWriteDescriptorSet> own = writes.get();
        const NotFiniteCensus* const census = pipeline.getCensus();
        if (census == nullptr)
        {
            vkCmdPushDescriptorSet(commands, pipeline.getBindPoint(), pipeline.getLayout(), Shaders::SET_PASS,
                static_cast<std::uint32_t>(own.size()), own.data());
            return;
        }

        const VkDescriptorBufferInfo counted = census->describe();
        std::array<VkWriteDescriptorSet, BindingTable::sMost + 1> all{};
        std::copy(own.begin(), own.end(), all.begin());
        all[own.size()] = VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = VK_NULL_HANDLE,
            .dstBinding = Shaders::BIND_CENSUS,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pImageInfo = nullptr,
            .pBufferInfo = &counted,
            .pTexelBufferView = nullptr,
        };
        vkCmdPushDescriptorSet(commands, pipeline.getBindPoint(), pipeline.getLayout(), Shaders::SET_PASS,
            static_cast<std::uint32_t>(own.size() + 1), all.data());
    }
}
