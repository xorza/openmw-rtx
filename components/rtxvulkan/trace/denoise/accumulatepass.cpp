#include "accumulatepass.hpp"

#include <array>
#include <cassert>

#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtx/shaders/accumulate.h>
#include <components/rtx/shaders/camera.h>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>

namespace Rtx
{
    namespace
    {
        /// The channel being blended, the two the frame describes it with, the three a history
        /// arrives in, the two of those this pass writes back, and the blend the cascade reads.
        /// Nine and not ten, because the first wavelet level writes the history this reads next
        /// frame — SVGF's feedback.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::ACCUMULATE_BINDINGS> sBindings
            = computeBindings<Shaders::ACCUMULATE_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }

    AccumulatePass::AccumulatePass(const Device& device, const std::filesystem::path& shaderDirectory)
        : mPipeline(device, sBindings, sizeof(Shaders::AccumulateConstants), {},
            shaderDirectory / "accumulate.comp.spv", "accumulate")
    {
    }

    void AccumulatePass::record(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::Camera& camera = frame.mSampled.mCamera;
        assert(images.mBlended.getWidth() >= camera.mWidth && images.mBlended.getHeight() >= camera.mHeight);

        DescriptorWrites<Shaders::ACCUMULATE_BINDINGS> writes;
        writes.image(Shaders::ACCUMULATE_BIND_INDIRECT, buffer.get(Channel::Indirect).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_MOTION, buffer.get(Channel::Motion).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_COLOUR, images.mColourBefore.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_SURFACE, images.mSurfaceBefore.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_MOMENTS, images.mMomentsBefore.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_SURFACE_OUT, images.mSurface.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_MOMENTS_OUT, images.mMoments.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_BLENDED_OUT, images.mBlended.describeStorage());
        assert(writes.size() == Shaders::ACCUMULATE_BINDINGS && "a binding the layout declares was left unwritten");

        const Shaders::AccumulateConstants constants{
            .mCamera = camera,
            .mReset = images.mFresh ? 1u : 0u,
            .mDistanceScale = frame.mDistanceScale,
        };

        dispatch(commands, mPipeline, writes.get(), constants, groupsFor(camera.mWidth, Shaders::ACCUMULATE_WORKGROUP),
            groupsFor(camera.mHeight, Shaders::ACCUMULATE_WORKGROUP));
    }
}
