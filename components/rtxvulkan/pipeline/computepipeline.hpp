#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include <volk.h>

#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/owned.hpp>

#include "pipeline.hpp"

namespace Rtx
{
    class Device;
    class ShaderCode;

    /// How many lanes a compute pipeline's subgroup holds: the driver's choice, or 32 asked for. **32
    /// for a kernel that traces**, AMD's advice for ray queries on RDNA, where a driver may choose 64
    /// for compute; NVIDIA's warp is 32 whatever is asked. The spill gate's register ceiling is a
    /// 32-lane figure (`RtxVisibilityKernelsTest`). **Asked in full subgroups**, so a workgroup of 32
    /// is one subgroup, which a ballot over it relies on; its first side must then be a multiple of
    /// 32 (`TRACE_WORKGROUP_LANES`).
    enum class SubgroupSize
    {
        Driver,
        ThirtyTwo,
    };

    /// A compute pipeline's handle against `layout`: the part of `ComputePipeline` its constants do
    /// not decide.
    ///
    /// @param module the compiled SPIR-V the build wrote, by its name in the device's shader
    ///        directory.
    /// @param specialization one word per specialization constant, `constant_id` `i` taking
    ///        `specialization[i]` — a `bool` reaches SPIR-V as a 32-bit value like a `uint`.
    Owned<VkPipeline, vkDestroyPipeline> makeComputePipeline(const Device& device, const PipelineLayout& layout,
        std::string_view module, std::string_view name, std::span<const std::uint32_t> specialization,
        SubgroupSize lanes);

    /// The same, of a module `code` read already: one read for the pipelines a pass makes of one
    /// module under several constants (`ShaderCode`).
    Owned<VkPipeline, vkDestroyPipeline> makeComputePipeline(const Device& device, const PipelineLayout& layout,
        const ShaderCode& code, std::string_view module, std::string_view name,
        std::span<const std::uint32_t> specialization, SubgroupSize lanes);

    /// A compute pipeline and its layout, pushed a `Constants`. `TracePipeline` is the same object
    /// for a launch.
    template <class Constants>
    class ComputePipeline : public TypedPipeline<Constants>
    {
    public:
        /// Nothing passed outlives the call.
        ///
        /// @param shared the shared sets the pipeline reads. A pipeline layout has to name every set
        ///        it will ever be handed.
        /// @param name what a capture calls the pipeline.
        ComputePipeline(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings,
            const SharedSetLayouts& shared, std::string_view module, std::string_view name,
            std::span<const std::uint32_t> specialization = {}, SubgroupSize lanes = SubgroupSize::Driver)
            : TypedPipeline<Constants>(
                PipelineLayout(device, bindings, pushRangeOf<Constants>(VK_SHADER_STAGE_COMPUTE_BIT), shared),
                [&](const PipelineLayout& layout) {
                    return makeComputePipeline(device, layout, module, name, specialization, lanes);
                },
                VK_PIPELINE_BIND_POINT_COMPUTE)
        {
        }

        /// The same, of a module `code` read already.
        ComputePipeline(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings,
            const SharedSetLayouts& shared, const ShaderCode& code, std::string_view module, std::string_view name,
            std::span<const std::uint32_t> specialization = {}, SubgroupSize lanes = SubgroupSize::Driver)
            : TypedPipeline<Constants>(
                PipelineLayout(device, bindings, pushRangeOf<Constants>(VK_SHADER_STAGE_COMPUTE_BIT), shared),
                [&](const PipelineLayout& layout) {
                    return makeComputePipeline(device, layout, code, module, name, specialization, lanes);
                },
                VK_PIPELINE_BIND_POINT_COMPUTE)
        {
        }
    };
}
