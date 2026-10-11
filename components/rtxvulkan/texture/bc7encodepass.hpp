#pragma once

#include <array>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/bc7.h>

namespace Rtx
{
    class Buffer;
    class Device;
    class Image;

    /// Where each level of a chain's BC7 blocks stands in a buffer of them: level by level, each row
    /// by row, a level under a block taking one.
    struct Bc7Chain
    {
        struct Level
        {
            VkDeviceSize mOffset = 0;
            std::uint32_t mWidth = 0;
            std::uint32_t mHeight = 0;
            std::uint32_t mBlocks = 0;
        };

        std::array<Level, Shaders::BC7_MOST_LEVELS> mLevels{};
        std::uint32_t mCount = 0;
        VkDeviceSize mBytes = 0;

        /// The chain of `levels` from `width` by `height`, each level half the one above and never
        /// under a texel.
        static constexpr Bc7Chain of(std::uint32_t width, std::uint32_t height, std::uint32_t levels)
        {
            Bc7Chain chain;
            chain.mCount = levels;
            for (std::uint32_t at = 0; at < levels; ++at)
            {
                Level& level = chain.mLevels[at];
                level.mOffset = chain.mBytes;
                level.mWidth = width;
                level.mHeight = height;
                level.mBlocks = ((width + Shaders::BC7_BLOCK_SIDE - 1) / Shaders::BC7_BLOCK_SIDE)
                    * ((height + Shaders::BC7_BLOCK_SIDE - 1) / Shaders::BC7_BLOCK_SIDE);
                chain.mBytes += VkDeviceSize{ level.mBlocks } * Shaders::BC7_BLOCK_BYTES;
                width = width > 1 ? width / 2 : 1;
                height = height > 1 ? height / 2 : 1;
            }
            return chain;
        }
    };

    /// Encodes an image the device made into BC7, on the device: `bc7encode.comp`, one dispatch for
    /// every level into a buffer of blocks, and one copy of them all into the compressed image. Shared by
    /// every scene; the buffer and the image are the caller's.
    class Bc7EncodePass
    {
    public:
        explicit Bc7EncodePass(const Device& device);

        /// Encodes every level of `source` into `blocks` from `offset`, laid out as `Bc7Chain::of`
        /// its shape lays them, and copies them into `target`, a BC7 image of the same shape.
        ///
        /// `source` is met readable as storage at every level, through views with no curve; `blocks`
        /// is met with nothing on the queue still using that range, and holds `Bc7Chain::mBytes`
        /// past `offset` at least; `target` is met undefined and left as a texture the trace samples.
        ///
        /// @param weighsAlpha whether a reader reads the alpha (`Bc7Constants::mWeighsAlpha`).
        void record(VkCommandBuffer commands, const Image& source, const Buffer& blocks, VkDeviceSize offset,
            const Image& target, bool weighsAlpha) const;

    private:
        ComputePipeline<Shaders::Bc7Constants> mPipeline;
    };
}
