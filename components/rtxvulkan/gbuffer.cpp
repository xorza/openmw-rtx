#include "gbuffer.hpp"

namespace Rtx
{
    namespace
    {
        /// Full floats for the three radiance channels, and half floats were tried first.
        ///
        /// **Eleven bits of mantissa is an eighth of a display byte**, which is the argument for
        /// halving fifty megabytes, and it is wrong in the one place that matters. A reference is a
        /// sum of a thousand frames, and rounding every term before adding it is exactly what the
        /// float accumulator exists to avoid — the error only averages away if it is random, and
        /// here it is not. Two reasons: `direct` is all but identical from frame to frame, so its
        /// rounding is a fixed offset that never averages at all; and the sampler is a
        /// low-discrepancy sequence rather than a random one, so even the terms that do vary vary
        /// in a pattern the rounding follows.
        ///
        /// Measured: the converged mean of a flat surface came out 0.096% low, against a tolerance
        /// of 0.067% that the test derives from what the format can show. Full floats put it back.
        constexpr VkFormat sRadiance = VK_FORMAT_R32G32B32A32_SFLOAT;

        /// The guide is full floats for a plainer reason.
        ///
        /// Its distance channel runs to thirty thousand units, where a half's steps are thirty-two
        /// units wide — coarser than most of what the filter is being asked to hold an edge across.
        constexpr VkFormat sGuide = VK_FORMAT_R32G32B32A32_SFLOAT;

        constexpr VkImageUsageFlags sUsage = VK_IMAGE_USAGE_STORAGE_BIT;
    }

    GBuffer::GBuffer(const Device& device, std::uint32_t width, std::uint32_t height)
        : mDirect(device, width, height, sRadiance, sUsage, "g-direct")
        , mIndirect(device, width, height, sRadiance, sUsage, "g-indirect")
        , mModulate(device, width, height, sRadiance, sUsage, "g-modulate")
        , mGuide(device, width, height, sGuide, sUsage, "g-guide")
    {
    }

    void GBuffer::begin(VkCommandBuffer commands) const
    {
        // From undefined, because every pixel of all four is written before any is read and there is
        // nothing in them worth carrying across a frame. Keeping the old contents would cost a
        // decompress on some hardware and buy a guarantee nothing here wants.
        //
        // **But waiting on the last frame's composite, which is not the same thing as discarding.**
        // One set of channels serves every frame, and a window keeps two in flight — so the trace
        // that is about to overwrite these may start while the composite reading them for the
        // previous frame is still running. Sourcing the barrier at the compute stage is the whole of
        // what a write-after-read needs; nothing has to be made visible, only ordered. Discarding
        // from `TOP_OF_PIPE` waits for nothing at all, and buys a torn frame for a barrier saved.
        for (const Image* image : { &mDirect, &mIndirect, &mModulate, &mGuide })
            image->transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    }

    void GBuffer::handOver(VkCommandBuffer commands) const
    {
        for (const Image* image : { &mDirect, &mIndirect, &mModulate, &mGuide })
            image->transition(commands, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    }
}
