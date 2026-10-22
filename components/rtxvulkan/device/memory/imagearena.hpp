#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "image.hpp"
#include "imageuse.hpp"
#include "memory.hpp"

namespace Rtx
{
    class Device;

    /// Where in a frame's fixed order an image is in use: its first step to its last, both included.
    /// Two images whose spans do not meet are never in use at once, so they may share memory.
    struct ArenaSpan
    {
        std::uint32_t mFirst = 0;
        std::uint32_t mLast = 0;

        constexpr bool meets(const ArenaSpan& other) const { return mFirst <= other.mLast && other.mFirst <= mLast; }

        bool operator==(const ArenaSpan& other) const = default;
    };

    /// One image an arena places: what it is, when it is in use, and what it is called.
    struct ArenaImage
    {
        ImageDescription mDescription;
        ArenaSpan mSpan;
        std::string_view mName;

        bool operator==(const ArenaImage& other) const = default;
    };

    /// What one image asks of the arena's memory, which is all the layout reads.
    struct ArenaNeed
    {
        VkDeviceSize mSize = 0;
        VkDeviceSize mAlignment = 1;
        ArenaSpan mSpan;
    };

    /// Images that share one allocation wherever their spans do not meet: the images a frame uses
    /// inside one stretch of it and never again — the trace's stage images, the denoiser's scratch,
    /// the upscaler's own — laid over each other, as a frame graph's transient resources are
    /// (O'Donnell, "FrameGraph", GDC 2017), through the placement VMA's "Resource aliasing"
    /// describes. Its other gains, scheduling across queues and reordering passes, are not wanted
    /// here: the order is fixed, and the second queue was declined.
    ///
    /// **One queue, and the barrier every command buffer opens with, are what the sharing rests
    /// on** (`CommandPool::open`): a frame's first use of the memory comes after the last frame's
    /// last. Inside one buffer, an image whose memory an earlier span used discards from
    /// `Use::sAliased`, which `firstUseOf` says; one that holds memory nothing used before it in the
    /// buffer discards from `Use::sUndefined`. A second queue would need an arena a frame slot.
    class ImageArena
    {
    public:
        ImageArena() = default;

        /// Lays `declared` out, takes one allocation for all of it as the frame's targets, and makes
        /// every image placed in it, each at its index in `declared`.
        ImageArena(const Device& device, std::span<const ArenaImage> declared);

        /// Buries the images and then the memory under them, in the order the graveyard ends them.
        ~ImageArena();

        ImageArena(const ImageArena&) = delete;
        ImageArena& operator=(const ImageArena&) = delete;

        /// Written out, because the memory must go after the images it holds: a defaulted assignment
        /// would hand the memory back first.
        ImageArena(ImageArena&& other) noexcept;
        ImageArena& operator=(ImageArena&& other) noexcept;

        /// What an arena of `declared` takes of the device's memory: the allocation's size.
        static VkDeviceSize bytesFor(const Device& device, std::span<const ArenaImage> declared);

        /// Places each of `needs` at the lowest offset its alignment allows where it overlaps no
        /// image whose span meets its own, the largest first, writing each offset at its index; and
        /// says how many bytes the placement covers. One image a span is the images side by side.
        static VkDeviceSize layOut(std::span<const ArenaNeed> needs, std::span<VkDeviceSize> offsets);

        bool isEmpty() const { return mImages.empty(); }

        std::size_t size() const { return mImages.size(); }

        /// The image made of `declared[at]`.
        const Image& get(std::size_t at) const
        {
            assert(at < mImages.size());
            return mImages[at];
        }

        /// What the image at `at` discards from at its first use in a frame: `Use::sAliased` where an
        /// image whose span ends before its own holds any of its memory, and `Use::sUndefined`
        /// where none does.
        const ImageUse& firstUseOf(std::size_t at) const
        {
            assert(at < mFirstUses.size());
            return *mFirstUses[at];
        }

    private:
        /// Buries what this holds, images first.
        void bury();

        const Device* mDevice = nullptr;
        DeviceMemory mMemory;
        std::vector<Image> mImages;
        std::vector<const ImageUse*> mFirstUses;
    };

    /// The images one owner declared into an arena, a run of it from `first`: what an owner holds
    /// instead of images of its own. The arena outlives it.
    class ArenaRun
    {
    public:
        ArenaRun() = default;

        ArenaRun(const ImageArena& arena, std::size_t first, std::size_t count)
            : mArena(&arena)
            , mFirst(first)
            , mCount(count)
        {
            assert(first + count <= arena.size() && "a run past the arena's end");
        }

        /// The part of `run` from `first`, `count` images of it: one part of an owner's run, for a part
        /// of the owner.
        ArenaRun(const ArenaRun& run, std::size_t first, std::size_t count)
            : mArena(run.mArena)
            , mFirst(run.mFirst + first)
            , mCount(count)
        {
            assert(first + count <= run.mCount && "a part past its run's end");
        }

        std::size_t size() const { return mCount; }

        const Image& operator[](std::size_t at) const
        {
            assert(at < mCount);
            return mArena->get(mFirst + at);
        }

        const ImageUse& firstUseOf(std::size_t at) const
        {
            assert(at < mCount);
            return mArena->firstUseOf(mFirst + at);
        }

    private:
        const ImageArena* mArena = nullptr;
        std::size_t mFirst = 0;
        std::size_t mCount = 0;
    };
}
