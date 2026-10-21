#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <volk.h>

#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/owned.hpp>

#include "pipeline.hpp"

namespace Rtx
{
    class Device;
    class ShaderCode;

    /// A ray tracing pipeline's handle against `layout`: the part of `TracePipeline` its constants
    /// do not decide.
    ///
    /// @param code where the stage's file was read, which holds `raygen`.
    /// @param specialization one word per specialization constant, as `ComputePipeline` takes them.
    Owned<VkPipeline, vkDestroyPipeline> makeTracePipeline(const Device& device, const PipelineLayout& layout,
        const ShaderCode& code, std::string_view raygen, std::string_view name,
        std::span<const std::uint32_t> specialization);

    /// The one record a launch reads its shader out of: the ray generation group's handle, in
    /// video memory the host wrote it straight into, and the regions a launch is handed, the three
    /// beside it empty.
    class ShaderBindingTable
    {
    public:
        ShaderBindingTable(const Device& device, VkPipeline pipeline, std::string_view name);

        /// Launches `width` by `height` by `depth` invocations of the ray generation stage.
        void traceRays(
            VkCommandBuffer commands, std::uint32_t width, std::uint32_t height, std::uint32_t depth = 1) const;

        // Read by the tests and by nothing else.
        const Buffer& getBuffer() const { return mTable; }

    private:
        const Device& mDevice;
        Buffer mTable;

        VkStridedDeviceAddressRegionKHR mRaygen{};

        /// No miss, hit or callable stage, so these regions are empty and the launch is handed them
        /// anyway.
        VkStridedDeviceAddressRegionKHR mNone{};
    };

    /// A ray tracing pipeline of a ray generation stage alone, which traces with ray queries, and
    /// the shader binding table a launch reads it out of, pushed a `Constants`.
    ///
    /// **A launch and not a compute dispatch, though nothing but ray queries traces in it**: a ray
    /// query answers repeatably only inside a launch, which the card does not preempt mid-traversal
    /// (`fogscatter.rgen`). **And no hit, miss or any-hit stage**: AMD's Windows driver answers no
    /// ray query in a pipeline that also calls `traceRayEXT`
    /// (`docs/rtx/research/2026-10-19-amd-windows-driver-faults.md`).
    template <class Constants>
    class TracePipeline : public TypedPipeline<Constants>
    {
    public:
        /// Nothing passed outlives the call.
        ///
        /// @param bindings `SET_PASS`, which every binding declares every stage of this pipeline in.
        /// @param shared the shared sets the pipeline reads. A pipeline layout has to name every set it
        ///        will ever be handed.
        /// @param code where the stage's file was read, shared by the pipelines a pass makes of one
        ///        set of modules.
        /// @param raygen the compiled SPIR-V the build wrote, by name in the device's shader directory.
        /// @param name what a capture calls the pipeline.
        TracePipeline(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings,
            const SharedSetLayouts& shared, const ShaderCode& code, std::string_view raygen, std::string_view name,
            std::span<const std::uint32_t> specialization = {})
            : TypedPipeline<Constants>(
                PipelineLayout(device, bindings, pushRangeOf<Constants>(VK_SHADER_STAGE_RAYGEN_BIT_KHR), shared),
                [&](const PipelineLayout& layout) {
                    return makeTracePipeline(device, layout, code, raygen, name, specialization);
                },
                VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
            , mTable(device, this->getHandle(), name)
        {
        }

        void traceRays(
            VkCommandBuffer commands, std::uint32_t width, std::uint32_t height, std::uint32_t depth = 1) const
        {
            mTable.traceRays(commands, width, height, depth);
        }

        // Read by the tests and by nothing else.
        const Buffer& getTable() const { return mTable.getBuffer(); }

    private:
        ShaderBindingTable mTable;
    };
}
