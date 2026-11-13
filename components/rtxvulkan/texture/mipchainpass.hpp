#pragma once

#include <span>

#include <volk.h>

#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/mipchain.h>

namespace Rtx
{
    class Device;
    class Image;

    /// Fills every level below the first of an image a dispatch wrote by halving the one above it: a box, which is
    /// what a moment wants, a channel carrying a square averaging to a mean square; in light, through a display-encoded
    /// image's curve. One dispatch a chain by AMD's single-pass downsampler (`mipchain.comp`), where a blit a level
    /// drained the queue between levels (`docs/architecture.md`, "A chain under a written image is one dispatch").
    ///
    /// Each pass that writes a chain holds one, as it holds its other pipelines; the driver's cache makes the
    /// pipelines after the first a lookup.
    class MipChainPass
    {
    public:
        /// Zeroes the counters. Submits and waits.
        explicit MipChainPass(const Device& device);

        /// Builds the chain of each of `images`, every level of which is in `Use::sComputeWrite` with the first
        /// written, and leaves each whole in `Use::sShaderSample`. The chains side by side, no barrier between them,
        /// one before and one after. An image of one level is only ordered against the read, as every chain is.
        ///
        /// @param images at most `MIPCHAIN_CHAINS_MOST`, each square with a side a power of two of at most 4096
        ///        texels, a chain down to one texel, and storage usage.
        void record(VkCommandBuffer commands, std::span<const Image* const> images) const;

    private:
        ComputePipeline<Shaders::MipChainConstants> mPipeline;

        /// One word a chain of a record, `MIPCHAIN_COUNTER_STRIDE` apart, which its workgroups count themselves done
        /// at. Nought between dispatches:
        /// the last workgroup of each chain puts its own back, so the record orders this against the one before it
        /// and clears nothing.
        Buffer mCounters;
    };
}
