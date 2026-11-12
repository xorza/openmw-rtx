#pragma once

#include <cstdint>
#include <optional>

#include <vulkan/vulkan_core.h>

#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/notfinitekeep.h>

#include "digestimages.hpp"

namespace Rtx
{
    class Device;

    /// What a counting device keeps of the first frame whose census counted a store that was not finite by
    /// the digest's step (`NotFiniteFrame`, `shaders/shared/notfinitekeep.h`): the digest's images, each
    /// searched for its first texel that is not finite, and a window of every one around the first image's.
    /// Decided on the device as the frame runs, since the census's report comes back after later frames
    /// wrote over the images; every other frame dispatches nothing. Made only where the device counts.
    class NotFiniteKeep
    {
    public:
        explicit NotFiniteKeep(const Device& device);

        /// Records the keep of `images`, read at the digest's step, after every pass that wrote them and
        /// whatever stored into the census: `frame` is the frame's number as the renderer counts its frames,
        /// and `sampleFrame` its sample number, which says how its fields lie. The direct channel's slot
        /// holds the composed frame, as the digest's does.
        void record(
            VkCommandBuffer commands, const DigestImages& images, std::uint64_t frame, std::uint32_t sampleFrame) const;

        /// What a frame `record` recorded kept since the last call, once every frame that could have kept it
        /// was waited out, and nothing where none has; the next frame that stores one is kept after it.
        std::optional<NotFiniteFrame> take();

    private:
        ComputePipeline<Shaders::NotFiniteGateConstants> mGate;
        ComputePipeline<Shaders::NotFiniteKeepConstants> mFind;
        ComputePipeline<Shaders::NotFiniteKeepConstants> mKeep;

        /// `Shaders::NotFiniteKept`, zeroed as it is made and as it is taken, in memory the host reads: the
        /// device writes it on the frame it keeps, and the dispatches' sizes once a frame.
        Buffer mKept;

        /// What a slot nothing is bound at is bound as, a float view and a word one, which the dispatches
        /// are told not to read.
        Image mNoFloats;
        Image mNoWords;
    };
}
