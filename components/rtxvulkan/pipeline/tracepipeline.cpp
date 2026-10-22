#include "tracepipeline.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/result.hpp>

#include "pipeline.hpp"
#include "shadercode.hpp"

namespace Rtx
{
    Owned<VkPipeline, vkDestroyPipeline> makeTracePipeline(const Device& device, const PipelineLayout& layout,
        const ShaderCode& code, const std::string_view raygen, const std::string_view name,
        const std::span<const std::uint32_t> specialization)
    {
        PipelineCreation creation(device, name);

        // Kept until the pipeline is made, because the stage's info points into it: the module names
        // its word of the census.
        const Specialization constants(device, raygen, specialization);
        const VkPipelineShaderStageCreateInfo stage{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = code.stage(raygen, layout.getSetTables(), layout.getPushRange().size, specialization),
            .flags = 0,
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .module = VK_NULL_HANDLE,
            .pName = "main",
            .pSpecializationInfo = constants.getInfo(),
        };
        const VkRayTracingShaderGroupCreateInfoKHR group{
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .pNext = nullptr,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
            .generalShader = 0,
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR,
            .pShaderGroupCaptureReplayHandle = nullptr,
        };

        const VkRayTracingPipelineCreateInfoKHR pipeline{
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
            .pNext = creation.chain(nullptr),
            // The statistics are asked for and, on this driver, not answered: NVIDIA reports one
            // executable for every compute pipeline in this renderer and none at all for a ray
            // tracing one — `Device::reportPipeline` is where that shows. Asked for anyway,
            // because it costs the frame nothing and is what makes the report appear the day a
            // driver answers. No procedural geometry anywhere in this renderer, so no AABB is ever
            // traversed, which NVIDIA's best-practice list asks to promise "whenever possible".
            .flags = device.getPipelineFlags() | VK_PIPELINE_CREATE_RAY_TRACING_SKIP_AABBS_BIT_KHR,
            .stageCount = 1,
            .pStages = &stage,
            .groupCount = 1,
            .pGroups = &group,
            // Nought: the stage calls no `traceRayEXT`, and a ray query is not recursion.
            .maxPipelineRayRecursionDepth = 0,
            .pLibraryInfo = nullptr,
            .pLibraryInterface = nullptr,
            .pDynamicState = nullptr,
            .layout = layout.getHandle(),
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        VkPipeline made = VK_NULL_HANDLE;
        checkVk(device.getFunctions().mCreateRayTracingPipelines(
                    device.getHandle(), VK_NULL_HANDLE, device.getPipelineCache(), 1, &pipeline, nullptr, &made),
            "vkCreateRayTracingPipelinesKHR");
        Owned<VkPipeline, vkDestroyPipeline> handle(device, made);

        creation.finish(handle.get());
        return handle;
    }

    ShaderBindingTable::ShaderBindingTable(const Device& device, const VkPipeline pipeline, const std::string_view name)
        : mDevice(device)
    {
        const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& limits
            = device.getPhysicalDevice().getProperties().mRayTracingPipeline;

        // The ray generation region's size is its stride, and both are aligned to the base
        // alignment rather than the handle's.
        const VkDeviceSize stride = alignUp(
            alignUp(limits.shaderGroupHandleSize, limits.shaderGroupHandleAlignment), limits.shaderGroupBaseAlignment);

        // Filled as unnamed, because this runs on a compile thread while the main thread submits,
        // and nothing has named the table yet: `traceRays` is the first hand-out.
        mTable = Buffer::hostWritten(device, stride,
            VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, name);
        const std::span<std::byte> table = mTable.unnamed<std::byte>();
        std::memset(table.data(), 0, table.size());

        std::vector<std::uint8_t> handle(limits.shaderGroupHandleSize);
        checkVk(device.getFunctions().mGetRayTracingShaderGroupHandles(
                    device.getHandle(), pipeline, 0, 1, handle.size(), handle.data()),
            "vkGetRayTracingShaderGroupHandlesKHR");
        std::memcpy(table.data(), handle.data(), handle.size());

        // A shader binding table's buffer is placed at the base alignment by the allocator —
        // `alignmentOwedBy` asks for it — so its address is aligned without offsetting into it.
        // Asserted rather than worked around, because a table that has to be offset into is a
        // table this renderer does not have.
        const VkDeviceAddress base = mTable.getDeviceAddress();
        assert(base % limits.shaderGroupBaseAlignment == 0
            && "a shader binding table the device would not read from where it was put");

        mRaygen = VkStridedDeviceAddressRegionKHR{ .deviceAddress = base, .stride = stride, .size = stride };
    }

    void ShaderBindingTable::traceRays(
        VkCommandBuffer commands, std::uint32_t width, std::uint32_t height, std::uint32_t depth) const
    {
        // The region carries the table by address, taken once at the fill, so the launch is the
        // hand-out that names it: the table then outlives every trace that reads it.
        mTable.nameForNext();
        mDevice.getFunctions().mCmdTraceRays(commands, &mRaygen, &mNone, &mNone, &mNone, width, height, depth);
    }
}
