#pragma once

#include <array>
#include <cstddef>
#include <span>

#include <vulkan/vulkan_core.h>

#include "imageuse.hpp"

namespace Rtx
{
    /// A run of dependencies emitted as one `vkCmdPipelineBarrier2`: the G-buffer's fourteen
    /// channels change state together twice a frame, and a pass that clears a buffer and takes an
    /// image has one barrier to record and not two. No allocation, because this is a frame path; a
    /// batch that fills up emits what it holds and carries on.
    class Barriers
    {
    public:
        explicit Barriers(VkCommandBuffer commands)
            : mCommands(commands)
            , mImages(mOwnImages)
        {
        }

        /// Over the caller's room for image barriers: a run longer than any frame records, an
        /// arrival's hundreds of images, as one command. A run past the room emits what it holds
        /// and carries on, as one past the default's does.
        Barriers(VkCommandBuffer commands, std::span<VkImageMemoryBarrier2> images)
            : mCommands(commands)
            , mImages(images)
        {
        }

        /// Not copied or moved: the default's room is its own, and a copy would name the original's.
        Barriers(const Barriers&) = delete;
        Barriers& operator=(const Barriers&) = delete;

        void add(const VkImageMemoryBarrier2& barrier);
        void add(const VkBufferMemoryBarrier2& barrier);

        /// Merged into the run's one memory barrier: the union of two dependencies orders
        /// everything either orders, and the access each side names stays inside a stage its
        /// side names, so the union is as valid as either.
        void add(const VkMemoryBarrier2& barrier);

        /// Emits what has been added, and empties. Does nothing where nothing was added.
        void flush();

        // Read by the tests and by nothing else.
        std::size_t getImageCount() const { return mImageCount; }
        std::size_t getEmitted() const { return mEmitted; }
        const VkMemoryBarrier2* getMemory() const { return mHasMemory ? &mMemory : nullptr; }

    private:
        /// The longest runs this renderer has: the G-buffer's channels, which change state
        /// together twice a frame, and a test's pair of readings. A run longer than this emits
        /// what it holds and carries on, so the figures bound the arrays rather than the caller.
        static constexpr std::size_t sMostImages = 16;
        static constexpr std::size_t sMostBuffers = 4;

        VkCommandBuffer mCommands = VK_NULL_HANDLE;
        std::array<VkImageMemoryBarrier2, sMostImages> mOwnImages{};
        std::span<VkImageMemoryBarrier2> mImages;
        std::array<VkBufferMemoryBarrier2, sMostBuffers> mBuffers{};
        VkMemoryBarrier2 mMemory{};
        std::size_t mImageCount = 0;
        std::size_t mBufferCount = 0;
        bool mHasMemory = false;

        /// How many commands `flush` has emitted.
        std::size_t mEmitted = 0;
    };

    /// Orders one pass's writes against what reads or writes them next: a memory barrier over
    /// everything, recorded on its own.
    inline void handOver(VkCommandBuffer commands, const BufferUse& from, const BufferUse& to)
    {
        Barriers barriers(commands);
        barriers.add(memoryBarrier(from, to));
        barriers.flush();
    }
}
