#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtx/renderer/denoiserimage.hpp>
#include <components/rtx/renderer/framedigest.hpp>
#include <components/rtx/shaders/digest.h>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/channeldigest.h>

namespace Rtx
{
    class Device;
    class GpuTimer;

    /// The frame's images folded into `FrameDigest`'s words on the device — `shaders/digest.h`.
    /// One shader, whatever the images' formats, because the load converts from each view's and the
    /// digest takes the bits of what came out; a dispatch a run of slots (`DIGEST_FLOAT_SLOTS`).
    class DigestPass
    {
    public:
        /// What `record` copies into its buffer, which is what that buffer has to have room for.
        static constexpr VkDeviceSize sBytes
            = sizeof(std::uint32_t) * Shaders::DIGEST_ALL_IMAGES * Shaders::DIGEST_LANES;

        explicit DigestPass(const Device& device);

        /// `channels` and the bound images of `denoiser` into `into`, ordered for a host read last:
        /// image `i` of the channels fills the `DIGEST_LANES` words from `i * DIGEST_LANES`, and the
        /// denoiser's images follow (`lanesOf`). Every image is at one extent and readable as a
        /// storage image in `GENERAL`; the denoiser's, null where the frame wrote none, are ordered
        /// here behind the passes that wrote them. Hands back which of them it took, a bit at each
        /// one's index (`FrameDigest::mDenoiserTaken`).
        std::uint32_t record(VkCommandBuffer commands, const std::array<const Image*, Shaders::DIGEST_IMAGES>& channels,
            const std::array<const Image*, sDenoiserImageCount>& denoiser, const Buffer& into, GpuTimer* timer) const;

        /// The words `record` copied into `lanes`, into `into`: the channels', and the denoiser's
        /// where `into.mDenoiserTaken` says it took them.
        static void unpack(const Buffer& lanes, FrameDigest& into);

    private:
        /// Where a denoiser image is bound and its words lie: among the floats after the channels,
        /// or among the words after those, by what its image holds. Its place among its own kind.
        struct Slot
        {
            bool mWords;
            std::uint32_t mAt;
        };
        static Slot slotOf(DenoiserImage image);

        /// Where a denoiser image's words lie in the lanes, counted in images.
        static std::size_t lanesOf(DenoiserImage image);

        ComputePipeline<Shaders::DigestConstants> mPipeline;

        /// Where the words are folded, in the device's own memory, and copied out of once:
        /// folded straight into the frame's read-back buffer, every atomic was a transaction across
        /// the bus, and the pass took twenty-four milliseconds a frame. One, and not one a frame in
        /// flight, because the copy out of it stands in the queue before the next frame's clear.
        Buffer mLanes;

        /// What a denoiser image the frame wrote none of is bound as, a float view and a word one,
        /// which the dispatch is told not to read.
        Image mNoFloats;
        Image mNoWords;
    };
}
