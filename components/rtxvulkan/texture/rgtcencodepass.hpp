#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/rgtc.h>

namespace Rtx
{
    class Buffer;
    class Device;
    class Image;

    /// Encodes an image the device made into BC4 or BC5, on the device: `rgtcencode.comp`, one
    /// dispatch for every level into a buffer of blocks, and one copy of them all into the compressed
    /// image, as `Bc7EncodePass` does. For a chain of the one or two channels its file held.
    class RgtcEncodePass
    {
    public:
        explicit RgtcEncodePass(const Device& device);

        /// Encodes every level of `source`'s red, and its green where `target` is BC5, into `blocks`,
        /// laid out as `BlockChain::of` its shape lays them, and copies them into `target`.
        ///
        /// What each is met as is `Bc7EncodePass::record`'s.
        void record(VkCommandBuffer commands, const Image& source, const Buffer& blocks, const Image& target) const;

    private:
        ComputePipeline<Shaders::RgtcConstants> mPipeline;
        std::uint32_t mMostGroupsAcross = 0;
    };
}
