#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/bc7.h>

namespace Rtx
{
    class Buffer;
    class Device;
    class Image;

    /// Encodes an image the device made into BC7, on the device: `bc7encode.comp`, one dispatch for
    /// every level into a buffer of blocks, and one copy of them all into the compressed image. Shared by
    /// every scene; the buffer and the image are the caller's.
    class Bc7EncodePass
    {
    public:
        explicit Bc7EncodePass(const Device& device);

        /// Encodes every level of `source` into `blocks`, laid out as `BlockChain::of` its shape lays
        /// them, and copies them into `target`, a BC7 image of the same shape.
        ///
        /// `source` is met readable as storage at every level, through views with no curve; `blocks`
        /// is met with nothing on the queue still using it, and holds the chain's bytes at least;
        /// `target` is met undefined and left as a texture the trace samples.
        ///
        /// @param weighsAlpha whether a reader reads the alpha (`Bc7Constants::mWeighsAlpha`).
        void record(VkCommandBuffer commands, const Image& source, const Buffer& blocks, const Image& target,
            bool weighsAlpha) const;

    private:
        ComputePipeline<Shaders::Bc7Constants> mPipeline;
        std::uint32_t mMostGroupsAcross = 0;
    };
}
