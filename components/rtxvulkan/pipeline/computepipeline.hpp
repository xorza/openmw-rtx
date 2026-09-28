#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

#include <vulkan/vulkan_core.h>

#include "pipeline.hpp"

namespace Rtx
{
    class Device;

    /// A compute pipeline and its layout. `TracePipeline` is the same object for a launch.
    class ComputePipeline : public Pipeline
    {
    public:
        /// Nothing passed outlives the call.
        ///
        /// @param pushConstantBytes the whole range, at offset zero, visible to the compute stage.
        /// @param shared the shared sets the pipeline reads. A pipeline layout has to name every set
        ///        it will ever be handed.
        /// @param module the compiled SPIR-V the build wrote, by path.
        /// @param name what a capture calls the pipeline.
        /// @param specialization one word per specialization constant, `constant_id` `i` taking
        ///        `specialization[i]` — a `bool` reaches SPIR-V as a 32-bit value like a `uint`.
        ComputePipeline(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings,
            std::uint32_t pushConstantBytes, const SharedSetLayouts& shared, const std::filesystem::path& module,
            std::string_view name, std::span<const std::uint32_t> specialization = {});
    };
}
