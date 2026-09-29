#pragma once

#include <array>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/buffer.hpp>

#include "device.hpp"

namespace Rtx
{
    /// The checkpoints a queue's buffer markers name, by number. `VK_AMD_buffer_marker` writes a
    /// 32-bit number where NVIDIA's checkpoints carry a pointer, so the host keeps the last
    /// `sKept` it handed out and a number read back after a loss finds its checkpoint here.
    class MarkerRing
    {
    public:
        /// How many numbers name their checkpoint at once: every zone of every frame in flight, many
        /// times over.
        static constexpr std::uint32_t sKept = 256;

        /// The number the next marker writes, which names `checkpoint` until `sKept` more are handed
        /// out. Never nought, which is what a marker nothing wrote reads.
        std::uint32_t mark(const Checkpoint* checkpoint)
        {
            ++mCount;
            if (mCount == 0)
                ++mCount;

            mCheckpoints[mCount % sKept] = checkpoint;
            return mCount;
        }

        /// The checkpoint `marker` names, or null where it names none: nought, and a number handed
        /// out so long ago that its place was taken again.
        const Checkpoint* find(std::uint32_t marker) const
        {
            if (marker == 0 || mCount - marker >= sKept)
                return nullptr;

            return mCheckpoints[marker % sKept];
        }

    private:
        std::array<const Checkpoint*, sKept> mCheckpoints{};
        std::uint32_t mCount = 0;
    };

    /// AMD's breadcrumbs, `VK_AMD_buffer_marker`, where the driver has no NVIDIA checkpoints: as
    /// each zone opens, its number is written at the top of the pipe, which the queue passes as it
    /// reaches the zone, and at the bottom, which it passes once everything before the zone is done
    /// — the two stages AMD's breadcrumbs read. After a loss the two words say how far the queue got.
    class BufferMarkers
    {
    public:
        /// @param write the device's `vkCmdWriteBufferMarkerAMD`.
        BufferMarkers(const Device& device, PFN_vkCmdWriteBufferMarkerAMD write);

        /// Marks the queue's progress with `checkpoint`, both words.
        void mark(VkCommandBuffer commands, const Checkpoint* checkpoint);

        /// The checkpoints the two words name, the top of the pipe's and the bottom's: null where a
        /// word names none the ring still holds.
        struct Passed
        {
            const Checkpoint* mTop;
            const Checkpoint* mBottom;
        };

        /// What the words hold as the device left them. After a loss, which ended every write: the
        /// words are host memory of the cached kind, which the device writes across the bus
        /// coherently.
        Passed read() const;

    private:
        PFN_vkCmdWriteBufferMarkerAMD mWrite;
        Buffer mWords;
        MarkerRing mRing;
    };
}
