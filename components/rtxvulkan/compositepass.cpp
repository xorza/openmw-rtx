#include "compositepass.hpp"

#include <array>
#include <cassert>

#include "device.hpp"
#include "gbuffer.hpp"
#include "image.hpp"
#include "result.hpp"
#include "shadermodule.hpp"

namespace Rtx
{
    namespace
    {
        std::uint32_t groupsFor(std::uint32_t extent)
        {
            return (extent + Shaders::COMPOSITE_WORKGROUP - 1) / Shaders::COMPOSITE_WORKGROUP;
        }
    }

    CompositePass::CompositePass(const Device& device, const std::filesystem::path& shaderDirectory)
        : mDevice(device)
    {
        constexpr auto compute = VK_SHADER_STAGE_COMPUTE_BIT;
        constexpr auto image = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        constexpr std::array<VkDescriptorSetLayoutBinding, 5> bindings{
            VkDescriptorSetLayoutBinding{ 0, image, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 1, image, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 2, image, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 3, image, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 4, image, 1, compute, nullptr },
        };

        const VkDescriptorSetLayoutCreateInfo layout{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
            .bindingCount = static_cast<std::uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        checkVk(vkCreateDescriptorSetLayout(device.getHandle(), &layout, nullptr, &mSetLayout),
            "vkCreateDescriptorSetLayout");

        const VkPushConstantRange range{
            .stageFlags = compute,
            .size = sizeof(Shaders::CompositeConstants),
        };
        const VkPipelineLayoutCreateInfo pipelineLayout{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &mSetLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range,
        };
        checkVk(vkCreatePipelineLayout(device.getHandle(), &pipelineLayout, nullptr, &mPipelineLayout),
            "vkCreatePipelineLayout");

        const ShaderModule module(device, shaderDirectory / "composite.comp.spv");
        const VkComputePipelineCreateInfo pipeline{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = compute,
                .module = module.getHandle(),
                .pName = "main",
            },
            .layout = mPipelineLayout,
        };
        checkVk(
            vkCreateComputePipelines(device.getHandle(), device.getPipelineCache(), 1, &pipeline, nullptr, &mPipeline),
            "vkCreateComputePipelines");

        device.setName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(mPipeline), "composite");
    }

    CompositePass::~CompositePass()
    {
        if (mPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(mDevice.getHandle(), mPipeline, nullptr);
        if (mPipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(mDevice.getHandle(), mPipelineLayout, nullptr);
        if (mSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(mDevice.getHandle(), mSetLayout, nullptr);
    }

    void CompositePass::record(VkCommandBuffer commands, const GBuffer& buffer, const Image& history,
        const Image& target, const Shaders::CompositeConstants& constants) const
    {
        assert(buffer.getWidth() >= constants.mWidth && buffer.getHeight() >= constants.mHeight);
        assert(history.getWidth() >= constants.mWidth && history.getHeight() >= constants.mHeight);
        assert(target.getWidth() >= constants.mWidth && target.getHeight() >= constants.mHeight);

        const std::array<VkDescriptorImageInfo, 5> images{
            VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getDirect().getView(), VK_IMAGE_LAYOUT_GENERAL },
            VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getIndirect().getView(), VK_IMAGE_LAYOUT_GENERAL },
            VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getModulate().getView(), VK_IMAGE_LAYOUT_GENERAL },
            VkDescriptorImageInfo{ VK_NULL_HANDLE, history.getView(), VK_IMAGE_LAYOUT_GENERAL },
            VkDescriptorImageInfo{ VK_NULL_HANDLE, target.getView(), VK_IMAGE_LAYOUT_GENERAL },
        };

        std::array<VkWriteDescriptorSet, 5> writes{};
        for (std::uint32_t i = 0; i < images.size(); ++i)
            writes[i] = VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = i,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &images[i],
            };

        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline);
        vkCmdPushDescriptorSet(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineLayout, 0,
            static_cast<std::uint32_t>(writes.size()), writes.data());
        vkCmdPushConstants(commands, mPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commands, groupsFor(constants.mWidth), groupsFor(constants.mHeight), 1);
    }
}
