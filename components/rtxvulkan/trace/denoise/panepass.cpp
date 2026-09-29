#include "panepass.hpp"

#include <array>
#include <cassert>

#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtx/shaders/pane.h>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::PANE_BINDINGS> sBindings
            = computeBindings<Shaders::PANE_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }

    PanePass::PanePass(const Device& device, const std::filesystem::path& shaderDirectory)
        : mPipeline(device, sBindings, sizeof(Shaders::PaneConstants), {}, shaderDirectory / "pane.comp.spv", "pane")
    {
    }

    const Image& PanePass::record(
        VkCommandBuffer commands, const PaneHistory::Turn& turn, const GBuffer& buffer, const Frame& frame) const
    {
        const std::uint32_t width = frame.mCamera.mWidth;
        const std::uint32_t height = frame.mCamera.mHeight;
        assert(turn.mMean.getWidth() >= width && turn.mMean.getHeight() >= height);

        // As the glossy filter's: what this frame writes whole is discarded, and the history too
        // where there is none, so the reset is a statement about the history and not about the memory.
        {
            Barriers barriers(commands);
            if (turn.mFresh)
                for (const Image* image : { &turn.mMeanBefore, &turn.mFramesBefore, &turn.mHeldBefore })
                    barriers.add(image->describeTransition(Use::sUndefined, Use::sComputeRead));

            for (const Image* image : { &turn.mMean, &turn.mFrames, &turn.mHeld })
                barriers.add(image->describeTransition(Use::sUndefined, Use::sComputeWrite));

            barriers.flush();
        }

        DescriptorWrites<Shaders::PANE_BINDINGS> writes;
        writes.image(Shaders::PANE_BIND_PANE, buffer.get(Channel::Pane).describeStorage());
        writes.image(Shaders::PANE_BIND_SURFACE, buffer.get(Channel::PaneSurface).describeStorage());
        writes.image(Shaders::PANE_BIND_MOTION, buffer.get(Channel::PaneMotion).describeStorage());
        writes.image(Shaders::PANE_BIND_HELD_BEFORE, turn.mHeldBefore.describeStorage());
        writes.image(Shaders::PANE_BIND_HELD, turn.mHeld.describeStorage());
        writes.image(Shaders::PANE_BIND_MEAN_BEFORE, turn.mMeanBefore.describeStorage());
        writes.image(Shaders::PANE_BIND_FRAMES_BEFORE, turn.mFramesBefore.describeStorage());
        writes.image(Shaders::PANE_BIND_MEAN, turn.mMean.describeStorage());
        writes.image(Shaders::PANE_BIND_FRAMES, turn.mFrames.describeStorage());
        assert(writes.size() == Shaders::PANE_BINDINGS && "a binding the layout declares was left unwritten");

        const Shaders::PaneConstants constants{
            .mCamera = frame.mCamera,
            .mReset = (frame.mReset || turn.mFresh) ? 1u : 0u,
            .mDistanceScale = frame.mDistanceScale,
        };

        dispatch(commands, mPipeline, writes.get(), constants, groupsFor(width, Shaders::PANE_WORKGROUP),
            groupsFor(height, Shaders::PANE_WORKGROUP));

        turn.mMean.transition(commands, Use::sComputeWrite, Use::sComputeRead);
        return turn.mMean;
    }
}
