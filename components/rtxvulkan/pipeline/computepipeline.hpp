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

    /// A compute pipeline's handle against `layout`: the part of `ComputePipeline` its constants do
    /// not decide.
    ///
    /// @param module the compiled SPIR-V the build wrote, by its name in the device's shader
    ///        directory.
    /// @param specialization one word per specialization constant, `constant_id` `i` taking
    ///        `specialization[i]` — a `bool` reaches SPIR-V as a 32-bit value like a `uint`.
    Owned<VkPipeline, vkDestroyPipeline> makeComputePipeline(const Device& device, VkPipelineLayout layout,
        std::string_view module, std::string_view name, std::span<const std::uint32_t> specialization);

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
            std::span<const std::uint32_t> specialization = {})
            : ComputePipeline(device,
                PipelineLayout(device, bindings, pushRangeOf<Constants>(VK_SHADER_STAGE_COMPUTE_BIT), shared), module,
                name, specialization)
        {
        }

    private:
        ComputePipeline(const Device& device, PipelineLayout&& layout, std::string_view module, std::string_view name,
            std::span<const std::uint32_t> specialization)
            : TypedPipeline<Constants>(std::move(layout),
                makeComputePipeline(device, layout.getHandle(), module, name, specialization),
                VK_PIPELINE_BIND_POINT_COMPUTE)
        {
        }
    };
}
