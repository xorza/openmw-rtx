#pragma once

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace Rtx
{
    /// One table of fixed-size elements, kept as a list of separate buffers of a fixed count each.
    ///
    /// **What makes a scene appendable.** A table sized to the scene has to be made again when the
    /// scene grows, and everything already built from it — an acceleration structure holds the
    /// address of the vertices it was built from — is then pointing at memory that has moved. A
    /// block is allocated once at its full size and never reallocated, so every address handed out
    /// stays good and a cell arriving adds a block rather than replacing the world.
    ///
    /// **Full blocks even where the scene stops part way into the last one**, which is the whole of
    /// what the paragraph above buys: a block cut to what is currently in it would have to be made
    /// again the moment anything more arrived. The slack is bounded by one block per table.
    ///
    /// `Rtx::SceneDesc` never lets a mesh's run straddle a block, so `addressOf` on a run's first
    /// element covers the whole run.
    template <class Block>
    class BlockedBuffer
    {
    public:
        /// @param blockSize elements in a block, which the shader knows as `VERTEX_BLOCK` or
        ///        `INDEX_BLOCK` and divides by.
        /// @param stride bytes an element occupies.
        BlockedBuffer(std::uint32_t blockSize, std::uint32_t stride)
            : mBlockSize(blockSize)
            , mStride(stride)
        {
            assert(blockSize > 0 && stride > 0);
        }

        std::uint32_t getBlockSize() const { return mBlockSize; }
        std::uint32_t getStride() const { return mStride; }

        /// Bytes one whole block occupies, which is what every block is made at.
        VkDeviceSize getBlockBytes() const { return VkDeviceSize{ mBlockSize } * mStride; }

        /// How many blocks `elements` of them need. At least one, because a table nothing has been
        /// put in still has to be bound.
        std::uint32_t blocksFor(std::uint32_t elements) const
        {
            return elements == 0 ? 1u : (elements + mBlockSize - 1) / mBlockSize;
        }

        /// Which block an element is in, and how far into it, in bytes.
        std::uint32_t blockOf(std::uint32_t element) const { return element / mBlockSize; }
        VkDeviceSize offsetOf(std::uint32_t element) const { return VkDeviceSize{ element % mBlockSize } * mStride; }

        void add(Block&& block)
        {
            mAddresses.push_back(block.getDeviceAddress());
            mBlocks.push_back(std::move(block));
        }

        /// The block an element is in. **A contract**: a scene that has grown past what has been
        /// added here is one whose caller should have built the blocks again, not written into a
        /// block that does not exist.
        Block& at(std::uint32_t element)
        {
            assert(blockOf(element) < mBlocks.size());
            return mBlocks[blockOf(element)];
        }

        /// Where an element sits on the device, for a builder that reads it directly.
        VkDeviceAddress addressOf(std::uint32_t element) const
        {
            assert(blockOf(element) < mAddresses.size());
            return mAddresses[blockOf(element)] + offsetOf(element);
        }

        /// Where every block starts, in block order — what a shader is handed so it can resolve a
        /// global id itself.
        std::span<const VkDeviceAddress> getAddresses() const { return mAddresses; }

        VkDeviceSize getBytes() const { return getBlockBytes() * mBlocks.size(); }

    private:
        std::uint32_t mBlockSize;
        std::uint32_t mStride;

        std::vector<Block> mBlocks;

        /// Kept beside the blocks rather than asked for, because a table of them is written to the
        /// device and a device address is a driver call.
        std::vector<VkDeviceAddress> mAddresses;
    };
}
