#include "computepipeline.hpp"

#include <array>
#include <string_view>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/result.hpp>

#include "shadercode.hpp"

namespace Rtx
{
    Owned<VkPipeline, vkDestroyPipeline> makeComputePipeline(const Device& device, const PipelineLayout& layout,
        const std::string_view module, const std::string_view name, const std::span<const std::uint32_t> specialization)
    {
        PipelineCreation creation(device, name);
        const std::array<std::string_view, 1> modules{ module };
        const ShaderCode code(device, modules);
        const Specialization constants(device, module, specialization);

        const VkComputePipelineCreateInfo pipeline{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = creation.chain(nullptr),
            .flags = PipelineCreation::sFlags,
            .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = code.stage(module, layout.getSetTables(), layout.getPushRange().size, specialization),
                .flags = 0,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = VK_NULL_HANDLE,
                .pName = "main",
                .pSpecializationInfo = constants.getInfo(),
            },
            .layout = layout.getHandle(),
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        VkPipeline made = VK_NULL_HANDLE;
        checkVk(vkCreateComputePipelines(device.getHandle(), device.getPipelineCache(), 1, &pipeline, nullptr, &made),
            "vkCreateComputePipelines");
        Owned<VkPipeline, vkDestroyPipeline> handle(device, made);

        creation.finish(handle.get());
        return handle;
    }
}
