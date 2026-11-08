#pragma once

#include <array>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/shaders/shared/blockencode.h>

namespace Rtx
{
    /// Where each level of a chain's blocks stands in a buffer of them: level by level, each row by
    /// row, a level under a block taking one. What the block encoder writes and copies from
    /// (`Bc7EncodePass`).
    struct BlockChain
    {
        struct Level
        {
            VkDeviceSize mOffset = 0;
            std::uint32_t mWidth = 0;
            std::uint32_t mHeight = 0;
            std::uint32_t mBlocks = 0;
        };

        std::array<Level, Shaders::BLOCK_ENCODE_MOST_LEVELS> mLevels{};
        std::uint32_t mCount = 0;
        VkDeviceSize mBytes = 0;

        /// The chain of `levels` from `width` by `height` of blocks of `blockBytes`, each level half
        /// the one above and never under a texel.
        static constexpr BlockChain of(
            std::uint32_t width, std::uint32_t height, std::uint32_t levels, std::uint32_t blockBytes)
        {
            BlockChain chain;
            chain.mCount = levels;
            for (std::uint32_t at = 0; at < levels; ++at)
            {
                Level& level = chain.mLevels[at];
                level.mOffset = chain.mBytes;
                level.mWidth = width;
                level.mHeight = height;
                level.mBlocks = ((width + Shaders::BLOCK_SIDE - 1) / Shaders::BLOCK_SIDE)
                    * ((height + Shaders::BLOCK_SIDE - 1) / Shaders::BLOCK_SIDE);
                chain.mBytes += VkDeviceSize{ level.mBlocks } * blockBytes;
                width = width > 1 ? width / 2 : 1;
                height = height > 1 ? height / 2 : 1;
            }
            return chain;
        }

        /// How many workgroups encode the chain: every level's blocks in whole groups of its own.
        constexpr std::uint32_t groups() const
        {
            std::uint32_t total = 0;
            for (std::uint32_t at = 0; at < mCount; ++at)
                total += (mLevels[at].mBlocks + Shaders::BLOCK_ENCODE_WORKGROUP - 1) / Shaders::BLOCK_ENCODE_WORKGROUP;
            return total;
        }
    };
}
