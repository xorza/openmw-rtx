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

    /// **How long a spare block stands unused before `StagingRing::shed` gives it back**, in submits: a frame submits
    /// two or three times (its placements, its trace, a picture's), so 600 are some 200 to 300 frames, three to ten
    /// seconds by the frame rate, as jemalloc gives a page back after it decays unused for ten
    /// ([`dirty_decay_ms`](https://jemalloc.net/jemalloc.3.html)). A block shed a frame after the crossing that grew it
    /// was made again at the next, each a device allocation the kernel clears on a frame
    /// (`docs/research/2026-11-17-longest-frames.md`).
    inline constexpr std::uint64_t sStagingIdleSubmits = 600;

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
        void give(std::size_t block, std::uint64_t readUntil) { mRetiring.hold(readUntil, block); }

        /// Every block given back under a value at or below `through` is free again, as of `finished`, the value
        /// the timeline is known to have passed, which `shed` cools it from. **Two values**: an idle queue frees what
        /// was given back under a value no submit signalled yet, by a `through` past every value, and a block stamped
        /// with that read as cooled the moment it was freed.
        void releaseThrough(std::uint64_t through, std::uint64_t finished);

        /// Lets go of the free blocks past the `keep` bytes the smallest of them hold together,
        /// burying each as a buffer is buried: after a load, the one stretch whose staging no frame
        /// needs again. **The ring otherwise settles at its busiest stretch**, and a world built in one
        /// submit is the busiest there is. The arrivals after grow it again to their own.
        void trim(VkDeviceSize keep);

        /// Lets go of the largest free block that stood unused for `sStagingIdleSubmits` by `finished`, where the
        /// ring holds more than `keep` bytes, and of nothing else: what a frame that hands nothing over calls, so a
        /// crossing that staged hundreds of megabytes gives them back a block a frame once they cool, and no one frame
        /// pays for all. Free blocks only.
        void shed(VkDeviceSize keep, std::uint64_t finished);

        /// Which of the spare blocks `spare` `shed` gives back by `finished`, as a position in `spare`, or nothing: the
        /// largest whose stamp `freedAt` is at least `sStagingIdleSubmits` behind, and of equal ones the lowest block.
        template <class SizeOf, class FreedAt>
        static std::optional<std::size_t> sheddable(
            std::span<const std::size_t> spare, SizeOf&& sizeOf, FreedAt&& freedAt, const std::uint64_t finished)
        {
            std::optional<std::size_t> best;
            for (std::size_t at = 0; at < spare.size(); ++at)
            {
                if (finished < freedAt(spare[at]) + sStagingIdleSubmits)
                    continue;
                const VkDeviceSize size = sizeOf(spare[at]);
                if (!best.has_value() || size > sizeOf(spare[*best])
                    || (size == sizeOf(spare[*best]) && spare[at] < spare[*best]))
                    best = at;
            }
            return best;
        }

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
        /// **The most blocks the ring stands at once**: each is at least `sStagingBlock`, so 512 of them are 4 GiB
        /// staged, past every frame's share and every loading screen's; more is a block nothing gave back.
        static constexpr std::size_t sBlocksMost = 512;

        std::vector<Buffer> mBlocks;
        Retiring<std::size_t> mRetiring;
        std::vector<std::size_t> mSpare;

        /// When each block was last given back, by the timeline value that freed it: what `shed` cools blocks by.
        std::vector<std::uint64_t> mFreedAt;

        /// What the blocks hold together, free and in use: what `shed` keeps to.
        VkDeviceSize mBytes = 0;

        /// The places `trim` and `shed` emptied, which the next block made takes, so a place a batch
        /// holds never moves.
        std::vector<std::size_t> mLetGo;
    };
}
