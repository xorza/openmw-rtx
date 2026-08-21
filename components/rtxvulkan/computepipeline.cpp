#include "computepipeline.hpp"

#include <vector>

#include "device.hpp"
#include "result.hpp"
#include "shadermodule.hpp"

namespace Rtx
{
    ComputePipeline::ComputePipeline(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings,
        std::uint32_t pushConstantBytes, std::span<const VkDescriptorSetLayout> laterSets,
        const std::filesystem::path& module, std::string_view name)
        : mDevice(device)
    {
        // The renderer's only hand-written unwind, and the reason this type exists: three handles
        // are made in sequence and any of the three can fail, so what the earlier ones took has to
        // be given back before the failure leaves this constructor.
        try
        {
            const VkDescriptorSetLayoutCreateInfo layout{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
                .bindingCount = static_cast<std::uint32_t>(bindings.size()),
                .pBindings = bindings.data(),
            };
            checkVk(vkCreateDescriptorSetLayout(mDevice.getHandle(), &layout, nullptr, &mSetLayout),
                "vkCreateDescriptorSetLayout");

            // Set zero is this pipeline's own; whatever the caller named follows it, in order.
            std::vector<VkDescriptorSetLayout> sets;
            sets.reserve(laterSets.size() + 1);
            sets.push_back(mSetLayout);
            sets.insert(sets.end(), laterSets.begin(), laterSets.end());

            const VkPushConstantRange range{
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .size = pushConstantBytes,
            };
            const VkPipelineLayoutCreateInfo pipelineLayout{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                .setLayoutCount = static_cast<std::uint32_t>(sets.size()),
                .pSetLayouts = sets.data(),
                .pushConstantRangeCount = 1,
                .pPushConstantRanges = &range,
            };
            checkVk(vkCreatePipelineLayout(mDevice.getHandle(), &pipelineLayout, nullptr, &mLayout),
                "vkCreatePipelineLayout");

            const ShaderModule compiled(mDevice, module);
            const VkComputePipelineCreateInfo pipeline{
                .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage = {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                    .module = compiled.getHandle(),
                    .pName = "main",
                },
                .layout = mLayout,
            };
            checkVk(vkCreateComputePipelines(
                        mDevice.getHandle(), mDevice.getPipelineCache(), 1, &pipeline, nullptr, &mHandle),
                "vkCreateComputePipelines");

            mDevice.setName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(mHandle), name);
        }
        catch (...)
        {
            destroy();
            throw;
        }
    }

    ComputePipeline::~ComputePipeline()
    {
        destroy();
    }

    void ComputePipeline::destroy()
    {
        if (mHandle != VK_NULL_HANDLE)
            vkDestroyPipeline(mDevice.getHandle(), mHandle, nullptr);
        if (mLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(mDevice.getHandle(), mLayout, nullptr);
        if (mSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(mDevice.getHandle(), mSetLayout, nullptr);
    }
}
