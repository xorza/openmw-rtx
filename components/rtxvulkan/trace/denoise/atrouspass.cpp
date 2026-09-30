#include "atrouspass.hpp"

#include <array>
#include <cassert>

#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtx/shaders/atrous.h>
#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/look.h>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>

namespace Rtx
{
    namespace
    {
        /// The channel coming in with its variance, which says where the edges in the light are,
        /// the channel going out, and the one that says where the edges in the surface are and which
        /// eye each pixel's ray left. All pushed. Sampled on the two this pass only reads, because a twenty-five tap
        /// gather wants the texture unit's cache — a few per cent of the cascade — and legal from
        /// `VK_IMAGE_LAYOUT_GENERAL`.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::ATROUS_BINDINGS> sBindings{
            computeBinding(Shaders::ATROUS_BIND_SOURCE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
            computeBinding(Shaders::ATROUS_BIND_FILTERED, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::ATROUS_BIND_SURFACE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
        };

        /// Both reads, because a level's inputs are sampled and its target is storage. An image
        /// is each in turn as the levels ping-pong, so a dependency that named one of the two would
        /// leave the other frame's access uncovered.
        constexpr VkAccessFlags2 sReads = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

        /// **One fetch of eight bytes a tap for the surface**, the normal's code and the distance,
        /// where a half-float guide and a two-float depth were two of sixteen. Measured on the
        /// default suite against the two channels: 3.19 ms of the cascade to 2.78 in the guild,
        /// twice, and within the run-to-run spread on the deck, 2.47 to 2.37 and 2.58 at noon and
        /// 2.17 to 2.14 and 1.78 at dawn — so the octahedral `normalize` at 125 taps costs less
        /// than the bytes it saves. An earlier try at eight bytes was recorded here as worse, with
        /// no record of how it read the normal; this measurement replaces it.
        ///
        /// A shared-memory tile with Dolp's permutation was measured and did not pay, because the
        /// pass costs the same per level whatever the stride and there is no locality to recover.
        /// What the pass spends is the two `exp` and the surface tap. A profiler is what the next
        /// attempt should start from.
    }

    AtrousPass::AtrousPass(const Device& device)
        : mPipeline(device, sBindings, {}, "atrous.comp.spv", "atrous")
    {
    }

    const Image& AtrousPass::record(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::Camera& camera = frame.mSampled.mCamera;
        const Image& blended = images.mBlended;
        const Image& history = images.mColour;
        const Image& scratch = images.mScratch;
        assert(scratch.getWidth() >= camera.mWidth && scratch.getHeight() >= camera.mHeight);
        assert(buffer.getWidth() >= camera.mWidth && buffer.getHeight() >= camera.mHeight);

        // One assignment and not eight, so the filter's rays and the trace's cannot come to
        // differ: this pass is handed the one struct, and the shader rebuilds the rays with the
        // trace's own `rayAt`. The scratch was discarded with the rest of the frame's images
        // (`DenoiseHistory::discard`); every level after the first reads what the one before
        // wrote, which is what the barriers below order.
        Shaders::AtrousConstants level{
            .mCamera = camera,
            .mArms = frame.mSampled.mArms,
            .mStep = 1,
        };

        // Three images take turns and not two, because the first level's answer is the mean the
        // accumulator reads next frame — SVGF's feedback — so the levels after it ping-pong between
        // the blend and the scratch and leave it alone.
        const Image* source = &blended;
        const Image* target = &history;

        for (std::uint32_t pass = 0; pass < Shaders::ATROUS_LEVELS; ++pass)
        {
            if (pass > 0)
            {
                // The level about to run reads what the last one wrote and overwrites what it read,
                // so both channels have to be ordered against it — the second is a write after a
                // read, which needs the stages named and nothing made visible.
                Barriers between(commands);
                for (const Image* image : { source, target })
                    between.add(image->describeTransition(
                        ImageUse{ VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | sReads },
                        ImageUse{ VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | sReads }));

                between.flush();
            }

            // Sampled from `GENERAL` on the three this pass only reads. A `SAMPLED_IMAGE`
            // descriptor names the image alone and no sampler, which is what `sBindings` declares.
            DescriptorWrites writes(mPipeline);
            writes.image(Shaders::ATROUS_BIND_SOURCE, source->describeSampled(VK_NULL_HANDLE));
            writes.image(Shaders::ATROUS_BIND_FILTERED, target->describeStorage());
            writes.image(Shaders::ATROUS_BIND_SURFACE, buffer.get(Channel::Surface).describeSampled(VK_NULL_HANDLE));

            level.mStep = 1u << pass;

            dispatch(commands, mPipeline, writes, level,
                Groups::covering(camera.mWidth, camera.mHeight, Shaders::ATROUS_WORKGROUP));

            // The next level reads what this one wrote, and writes whichever of the other two it is
            // not reading — the blend after the first level, and the scratch and the blend by turns
            // after that.
            source = target;
            target = pass == 0 ? &blended : (source == &blended ? &scratch : &blended);
        }

        // The cascade hands over what it wrote, because nothing after it does: with the last level
        // ordered against nothing, the composite ran beside the dispatch still writing it, and two
        // runs of one doll wrote different bytes over a thousand of its pixels.
        source->transition(commands, Use::sComputeWrite,
            ImageUse{ VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, sReads });

        // One swap past the last dispatch, so this is what that dispatch wrote.
        return *source;
    }
}
