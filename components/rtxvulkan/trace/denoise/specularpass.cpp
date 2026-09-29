#include "specularpass.hpp"

#include <array>
#include <cassert>

#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtx/shaders/specular.h>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SPECULAR_BINDINGS> sBindings
            = computeBindings<Shaders::SPECULAR_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }

    SpecularPass::SpecularPass(const Device& device, const std::filesystem::path& shaderDirectory)
        : mPipeline(device, sBindings, sizeof(Shaders::SpecularConstants), {}, shaderDirectory / "specular.comp.spv",
            "specular")
    {
    }

    const Image& SpecularPass::record(
        VkCommandBuffer commands, const SpecularHistory::Turn& turn, const GBuffer& buffer, const Frame& frame) const
    {
        const Shaders::VisibilityConstants& sampled = frame.mSampled;
        const std::uint32_t width = sampled.mCamera.mWidth;
        const std::uint32_t height = sampled.mCamera.mHeight;
        assert(turn.mMean.getWidth() >= width && turn.mMean.getHeight() >= height);

        // As the accumulator's: what this frame writes whole is discarded, and the history too where
        // there is none, so the reset is a statement about the history and not about the memory.
        {
            Barriers barriers(commands);
            if (turn.mFresh)
                for (const Image* image : { &turn.mMeanBefore, &turn.mFramesBefore })
                    barriers.add(image->describeTransition(Use::sUndefined, Use::sComputeRead));

            for (const Image* image : { &turn.mMean, &turn.mFrames })
                barriers.add(image->describeTransition(Use::sUndefined, Use::sComputeWrite));

            barriers.flush();
        }

        DescriptorWrites<Shaders::SPECULAR_BINDINGS> writes;
        writes.image(Shaders::SPECULAR_BIND_SPECULAR, buffer.get(Channel::Specular).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MOTION, buffer.get(Channel::Motion).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_PUFFS, buffer.get(Channel::Puffs).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_HELD_SURFACE, frame.mHeld.mImage.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MEAN_BEFORE, turn.mMeanBefore.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_FRAMES_BEFORE, turn.mFramesBefore.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MEAN, turn.mMean.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_FRAMES, turn.mFrames.describeStorage());
        assert(writes.size() == Shaders::SPECULAR_BINDINGS && "a binding the layout declares was left unwritten");

        const Shaders::SpecularConstants constants{
            .mCamera = sampled.mCamera,
            .mArms = sampled.mArms,
            .mPreviousForward = sampled.mPreviousForward,
            .mPreviousRight = sampled.mPreviousRight,
            .mPreviousUp = sampled.mPreviousUp,
            .mArmsSpread = sampled.mArmsSpread,
            .mReset = (frame.mReset || frame.mHeld.mFresh || turn.mFresh) ? 1u : 0u,
            .mDistanceScale = frame.mHeld.mDistanceScale,
        };

        dispatch(commands, mPipeline, writes.get(), constants, groupsFor(width, Shaders::SPECULAR_WORKGROUP),
            groupsFor(height, Shaders::SPECULAR_WORKGROUP));

        turn.mMean.transition(commands, Use::sComputeWrite, Use::sComputeRead);
        return turn.mMean;
    }
}
