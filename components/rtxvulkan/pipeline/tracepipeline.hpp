#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include <volk.h>

#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/owned.hpp>

#include "pipeline.hpp"

namespace Rtx
{
    class Device;

    /// One closest-hit stage: a module, and its own whole table of specialization words, indexed by
    /// `constant_id` as the pipeline's is — so three stages may be one module under three
    /// settings. Empty takes the pipeline's.
    struct HitShader
    {
        std::string_view mModule;
        std::span<const std::uint32_t> mSpecialization;
    };

    /// Which shader stands at each record of a trace's shader binding table. One miss record
    /// apiece, in the order given: a `traceRayEXT` that misses with miss index `i` runs entry `i`
    /// of `mMiss`. A closest-hit shader stands behind `mHitRecordsPerShader` records in turn, so a
    /// hit that selects record `i` runs entry `i / mHitRecordsPerShader` of `mHit`. Which index an
    /// instance names is the shader-table record offset its acceleration structure carries, plus
    /// whatever the trace adds.
    struct TraceShaders
    {
        std::string_view mRaygen;
        std::span<const std::string_view> mMiss{};
        std::span<const HitShader> mHit{};

        /// How many records each closest-hit shader stands behind.
        std::uint32_t mHitRecordsPerShader = 1;

        /// What each hit record carries after its handle, one block per record in record order,
        /// every block the same size — or nothing. In the record and not in the payload, because a
        /// record is read by the shader the hit selects, and the payload is what every hit carries.
        std::span<const std::byte> mHitRecordData{};

        /// The one any-hit shader every hit group names, or nothing where traversal has no
        /// candidate to ask about. One and not one per group, because whether a candidate landed in
        /// a hole is a fact about the surface and not about what will shade it.
        std::string_view mAnyHit{};
    };

    /// A ray tracing pipeline's handle against `layout`: the part of `TracePipeline` its constants
    /// do not decide.
    ///
    /// @param specialization one word per specialization constant, as `ComputePipeline` takes them:
    ///        every stage's, but a closest-hit stage that names its own.
    Owned<VkPipeline, vkDestroyPipeline> makeTracePipeline(const Device& device, VkPipelineLayout layout,
        const TraceShaders& shaders, std::string_view name, std::span<const std::uint32_t> specialization);

    /// The records a launch reads its shaders out of: every group's handle, in video memory the
    /// host wrote it straight into, and the three regions a launch is handed. Laid out by
    /// `TraceShaders`, filled from `pipeline`'s handles.
    class ShaderBindingTable
    {
    public:
        ShaderBindingTable(
            const Device& device, VkPipeline pipeline, const TraceShaders& shaders, std::string_view name);

        /// Launches `width` by `height` by `depth` invocations of the ray generation stage.
        void traceRays(
            VkCommandBuffer commands, std::uint32_t width, std::uint32_t height, std::uint32_t depth = 1) const;

        // Read by the tests and by nothing else.
        const Buffer& getBuffer() const { return mTable; }

    private:
        const Device& mDevice;
        Buffer mTable;

        VkStridedDeviceAddressRegionKHR mRaygen{};
        VkStridedDeviceAddressRegionKHR mMiss{};
        VkStridedDeviceAddressRegionKHR mHit{};

        /// Nothing is callable, so this region is empty and the launch is handed it anyway.
        VkStridedDeviceAddressRegionKHR mCallable{};
    };

    /// A ray tracing pipeline and the shader binding table a launch reads it out of, its ray
    /// generation stage pushed a `Constants`. A launch and not a dispatch, because a hit runs a
    /// shader picked by traversal rather than by a branch, so the divergent half of a trace becomes
    /// one small program per kind of hit. Nothing recurses: the shaders a launch invokes
    /// trace again with inline ray queries.
    template <class Constants>
    class TracePipeline : public TypedPipeline<Constants>
    {
    public:
        /// Nothing passed outlives the call.
        ///
        /// @param bindings `SET_PASS`, which every binding declares every stage of this pipeline in.
        /// @param shared the shared sets the pipeline reads. A pipeline layout has to name every set it
        ///        will ever be handed.
        /// @param shaders the compiled SPIR-V the build wrote, by name in the device's shader
        ///        directory.
        /// @param name what a capture calls the pipeline.
        TracePipeline(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings,
            const SharedSetLayouts& shared, const TraceShaders& shaders, std::string_view name,
            std::span<const std::uint32_t> specialization = {})
            : TracePipeline(device,
                PipelineLayout(device, bindings, pushRangeOf<Constants>(VK_SHADER_STAGE_RAYGEN_BIT_KHR), shared),
                shaders, name, specialization)
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
        TracePipeline(const Device& device, PipelineLayout&& layout, const TraceShaders& shaders, std::string_view name,
            std::span<const std::uint32_t> specialization)
            : TypedPipeline<Constants>(std::move(layout),
                makeTracePipeline(device, layout.getHandle(), shaders, name, specialization),
                VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
            , mTable(device, this->getHandle(), shaders, name)
        {
        }

        ShaderBindingTable mTable;
    };
}
