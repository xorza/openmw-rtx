#include "temporallayerpass.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <components/rtx/shaders/camera.h>
#include <components/rtxvulkan/frame/gbuffer.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/historyclamp.h>
#include <components/rtxvulkan/shaders/shared/pane.h>
#include <components/rtxvulkan/shaders/shared/specular.h>

namespace Rtx
{
    namespace
    {
        // The one binding list both kernels are dispatched with.
        static_assert(Shaders::SPECULAR_BINDINGS == Shaders::PANE_BINDINGS);
        static_assert(Shaders::SPECULAR_BIND_SPECULAR == Shaders::PANE_BIND_PANE);
        static_assert(Shaders::SPECULAR_BIND_SURFACE == Shaders::PANE_BIND_SURFACE);
        static_assert(Shaders::SPECULAR_BIND_MOTION == Shaders::PANE_BIND_MOTION);
        static_assert(Shaders::SPECULAR_BIND_HELD_SURFACE == Shaders::PANE_BIND_HELD_BEFORE);
        static_assert(Shaders::SPECULAR_BIND_MEAN_BEFORE == Shaders::PANE_BIND_MEAN_BEFORE);
        static_assert(Shaders::SPECULAR_BIND_MEAN == Shaders::PANE_BIND_MEAN);
        static_assert(Shaders::SPECULAR_BIND_FAST_BEFORE == Shaders::PANE_BIND_FAST_BEFORE);
        static_assert(Shaders::SPECULAR_BIND_FAST_BLENDED == Shaders::PANE_BIND_FAST_BLENDED);
        static_assert(Shaders::SPECULAR_WORKGROUP == Shaders::PANE_WORKGROUP);

        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SPECULAR_BINDINGS> sBindings
            = computeBindings<Shaders::SPECULAR_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        /// A row a `TemporalLayer`, in its order. The glossy layer stands on the surface the trace
        /// hit; the pane on the layer's own surface and motion, which the trace keeps for it.
        constexpr std::array<TemporalLayerRow, 2> sRows{ {
            { .mModule = "specular.comp.spv",
                .mName = "specular",
                .mSampled = Channel::Specular,
                .mSurface = Channel::Surface,
                .mMotion = Channel::Motion,
                .mClampLayer = Shaders::HISTORY_CLAMP_GLOSSY },
            { .mModule = "pane.comp.spv",
                .mName = "pane",
                .mSampled = Channel::Pane,
                .mSurface = Channel::PaneSurface,
                .mMotion = Channel::PaneMotion,
                .mClampLayer = Shaders::HISTORY_CLAMP_PANE },
        } };
    }

    TemporalLayerPass::TemporalLayerPass(const Device& device, const TemporalLayer layer)
        : mRow(sRows[static_cast<std::size_t>(layer)])
        , mPipeline(device, sBindings, {}, mRow.mModule, mRow.mName)
    {
    }

    void TemporalLayerPass::record(VkCommandBuffer commands, const DenoiseHistory::LayerImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::Camera& camera = frame.mSampled.mEyes.mWorld;
        assert(images.mMean.getWidth() >= camera.mWidth);
        assert(images.mMean.getHeight() >= camera.mHeight);

        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::SPECULAR_BIND_SPECULAR, buffer.get(mRow.mSampled).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_SURFACE, buffer.get(mRow.mSurface).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MOTION, buffer.get(mRow.mMotion).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_HELD_SURFACE, buffer.getHeld(mRow.mSurface).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MEAN_BEFORE, images.mMeanBefore.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MEAN, images.mMean.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_FAST_BEFORE, images.mFast.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_FAST_BLENDED, images.mFastBlended.describeStorage());

        dispatch(commands, mPipeline, writes, frame.history(images.mFresh),
            Groups::covering(camera.mWidth, camera.mHeight, Shaders::SPECULAR_WORKGROUP));
    }
}
