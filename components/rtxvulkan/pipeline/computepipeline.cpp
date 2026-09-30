#include "computepipeline.hpp"

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/result.hpp>

namespace Rtx
{
    Owned<VkPipeline, vkDestroyPipeline> makeComputePipeline(const Device& device, const VkPipelineLayout layout,
        const std::string_view module, const std::string_view name, const std::span<const std::uint32_t> specialization)
    {
        PipelineCreation creation(device, name);
        const ShaderModule compiled = loadShaderModule(device, module);
        const Specialization constants(specialization);

        const VkComputePipelineCreateInfo pipeline{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = creation.chain(nullptr),
            .flags = PipelineCreation::sFlags,
            .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = compiled.get(),
                .pName = "main",
                .pSpecializationInfo = constants.getInfo(),
            },
            .layout = layout,
        };
        Owned<VkPipeline, vkDestroyPipeline> handle;
        checkVk(vkCreateComputePipelines(device.getHandle(), device.getPipelineCache(), 1, &pipeline, nullptr,
                    handle.put(device.getHandle())),
            "vkCreateComputePipelines");

        creation.finish(handle.get());
        return handle;
    }
}
