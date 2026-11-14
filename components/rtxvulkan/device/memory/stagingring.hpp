#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <volk.h>

#include <components/rtxvulkan/device/retiring.hpp>

#include "buffer.hpp"

namespace Rtx
{
    class Device;

    /// How much staging a batch takes at a time, sized so a town's tens of megabytes of textures
    /// cost a few blocks rather than hundreds of buffers. An upload larger than a block is given a
    /// block of its own exactly its size, which the ring keeps like any other.
    inline constexpr VkDeviceSize sStagingBlock = 8 * 1024 * 1024;

    /// What `StagingRing::trim` leaves once a world is built: a town's tens of megabytes, the most a
    /// crossing commonly brings, so the arrivals after a load make few blocks before the ring
    /// settles again.
    inline constexpr VkDeviceSize sStagingKept = 8 * sStagingBlock;

    /// What every run inside a block starts on: the largest texel block of any format this renderer
    /// uploads, BC2's and BC3's sixteen bytes. `VkBufferImageCopy::bufferOffset` has to be a
    /// multiple of four and of the format's texel block.
    inline constexpr VkDeviceSize sStagingAlignment = 16;

    /// The device's staging: blocks a batch writes uploads into and copies out of, retired under the
    /// submit that last read them as the pool's command buffers are, so an arrival costs no staging
    /// buffer of its own. The ring settles at the busiest stretch so far, shrunk only by `trim` and
    /// `shed`. A block is named by its place in the ring, which never moves.
    class StagingRing
    {
    public:
        /// A block of at least `bytes` that nothing on the queue reads, made on `device` where none
        /// is free. Taken until `give`.
        std::size_t take(const Device& device, VkDeviceSize bytes);

        /// The block at a place.
        const Buffer& at(std::size_t block) const { return mBlocks[block]; }

        /// Gives a block back, read until the timeline has passed `readUntil`.
        void give(std::size_t block, std::uint64_t readUntil) { mRetiring.hold(readUntil, std::size_t{ block }); }

        /// Every block given back under a value at or below `finished` is free again.
        void releaseThrough(std::uint64_t finished);

        /// Lets go of the free blocks past the `keep` bytes the smallest of them hold together,
        /// burying each as a buffer is buried: after a load, the one stretch whose staging no frame
        /// needs again. **The ring otherwise settles at its busiest stretch**, and a world built in one
        /// submit is the busiest there is. The arrivals after grow it again to their own.
        void trim(VkDeviceSize keep);

        /// Lets go of the largest free block where the ring holds more than `keep` bytes, and of
        /// nothing else: what a frame that hands nothing over calls, so a crossing that staged hundreds
        /// of megabytes gives them back a block a frame, and no one frame pays for all. Free blocks only.
        void shed(VkDeviceSize keep);

        /// Which of the spare blocks `spare` an upload of `bytes` takes, as a position in `spare`, or
        /// nothing where none holds it: the smallest that does, and of equal ones the lowest block.
        /// **Best fit and not first**: first fit lent a 36 MiB block to a hundred bytes beside a free
        /// 8 MiB one, and the next large upload then made one more. The lowest of a tie, so the choice is
        /// the blocks' and not the order the queue gave them back in.
        ///
        /// @param sizeOf a block's size, by its index.
        template <class SizeOf>
        static std::optional<std::size_t> bestFit(
            std::span<const std::size_t> spare, SizeOf&& sizeOf, const VkDeviceSize bytes)
        {
            std::optional<std::size_t> best;
            VkDeviceSize bestSize = 0;
            for (std::size_t at = 0; at < spare.size(); ++at)
            {
                const VkDeviceSize size = sizeOf(spare[at]);
                if (size < bytes)
                    continue;
                if (!best.has_value() || size < bestSize || (size == bestSize && spare[at] < spare[*best]))
                {
                    best = at;
                    bestSize = size;
                }
            }
            return best;
        }

        // Read by the tests and by nothing else.
        std::size_t getBlockCount() const { return mBlocks.size() - mLetGo.size(); }
        VkDeviceSize getSize(VkBuffer block) const;

    private:
        std::vector<Buffer> mBlocks;
        Retiring<std::size_t> mRetiring;
        std::vector<std::size_t> mSpare;

        /// What the blocks hold together, free and in use: what `shed` keeps to.
        VkDeviceSize mBytes = 0;

        /// The places `trim` and `shed` emptied, which the next block made takes, so a place a batch
        /// holds never moves.
        std::vector<std::size_t> mLetGo;
    };
}
