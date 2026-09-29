#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace Rtx
{
    /// What a checkpoint on the queue points at: the zone the timer opened and the frame it was
    /// opened for, so a device loss can say "frame 83, `tlas`". Owned by the timer that set it
    /// and stable while the timer's slot lives, which is longer than a fault takes to be reported.
    struct Checkpoint
    {
        std::string_view mName;
        std::uint64_t mFrame = 0;
    };

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
}
