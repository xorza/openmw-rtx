#include "imagearena.hpp"

#include <algorithm>
#include <numeric>
#include <utility>

#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/device.hpp>

namespace Rtx
{
    namespace
    {
        /// What every image of `declared` asks, and the one request covering them all: the layout's
        /// size, the coarsest alignment, and the memory types every image takes.
        struct Laid
        {
            std::vector<VkDeviceSize> mOffsets;
            VkMemoryRequirements mWhole{};
            std::vector<ArenaNeed> mNeeds;
        };

        Laid layOutOn(const Device& device, const std::span<const ArenaImage> declared)
        {
            Laid laid;
            laid.mNeeds.reserve(declared.size());
            laid.mWhole.alignment = 1;
            laid.mWhole.memoryTypeBits = ~0u;
            for (const ArenaImage& image : declared)
            {
                const VkMemoryRequirements asked = Image::requirementsFor(device, image.mDescription);
                laid.mNeeds.push_back(
                    ArenaNeed{ .mSize = asked.size, .mAlignment = asked.alignment, .mSpan = image.mSpan });
                laid.mWhole.alignment = std::max(laid.mWhole.alignment, asked.alignment);
                laid.mWhole.memoryTypeBits &= asked.memoryTypeBits;
            }

            // No image an arena holds asks a memory type the others refuse on a device that
            // qualifies — every one is a colour image in optimal tiling — so a device that does is
            // one this was not written for.
            if (laid.mWhole.memoryTypeBits == 0)
                throw Unsupported("no memory type takes every image the frame's arena places");

            laid.mOffsets.resize(declared.size());
            laid.mWhole.size = ImageArena::layOut(laid.mNeeds, laid.mOffsets);
            return laid;
        }
    }

    ImageArena::ImageArena(const Device& device, const std::span<const ArenaImage> declared)
    {
        if (declared.empty())
            return;

        const Laid laid = layOutOn(device, declared);
        mMemory = device.getMemory().take(laid.mWhole, MemoryUse::Frame);

        mImages.reserve(declared.size());
        mFirstUses.reserve(declared.size());
        for (std::size_t at = 0; at < declared.size(); ++at)
        {
            mImages.emplace_back(device, declared[at].mDescription, mMemory, laid.mOffsets[at], declared[at].mName);

            const VkDeviceSize begin = laid.mOffsets[at];
            const VkDeviceSize end = begin + laid.mNeeds[at].mSize;
            bool aliases = false;
            for (std::size_t other = 0; other < declared.size(); ++other)
                aliases = aliases
                    || (laid.mNeeds[other].mSpan.mLast < laid.mNeeds[at].mSpan.mFirst && laid.mOffsets[other] < end
                        && begin < laid.mOffsets[other] + laid.mNeeds[other].mSize);
            mFirstUses.push_back(aliases ? &Use::sAliased : &Use::sUndefined);
        }
    }

    VkDeviceSize ImageArena::bytesFor(const Device& device, const std::span<const ArenaImage> declared)
    {
        if (declared.empty())
            return 0;
        return layOutOn(device, declared).mWhole.size;
    }

    VkDeviceSize ImageArena::layOut(const std::span<const ArenaNeed> needs, const std::span<VkDeviceSize> offsets)
    {
        assert(offsets.size() == needs.size());

        // The largest first, which leaves the small to fill what the large leave between them; ties
        // in declared order, so one list lays out one way.
        std::vector<std::size_t> order(needs.size());
        std::iota(order.begin(), order.end(), std::size_t{ 0 });
        std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) { return needs[a].mSize > needs[b].mSize; });

        std::vector<std::size_t> placed;
        placed.reserve(needs.size());
        VkDeviceSize covered = 0;
        for (const std::size_t at : order)
        {
            const ArenaNeed& need = needs[at];
            assert(need.mAlignment > 0 && (need.mAlignment & (need.mAlignment - 1)) == 0);

            // The lowest offset is nought or the end of an image in use beside it: an offset between
            // those that fits would have fitted at the end below it.
            VkDeviceSize best = ~VkDeviceSize{ 0 };
            const auto tryAt = [&](const VkDeviceSize candidate) {
                const VkDeviceSize offset = alignUp(candidate, need.mAlignment);
                if (offset >= best)
                    return;
                for (const std::size_t other : placed)
                    if (needs[other].mSpan.meets(need.mSpan) && offsets[other] < offset + need.mSize
                        && offset < offsets[other] + needs[other].mSize)
                        return;
                best = offset;
            };
            tryAt(0);
            for (const std::size_t other : placed)
                if (needs[other].mSpan.meets(need.mSpan))
                    tryAt(offsets[other] + needs[other].mSize);

            offsets[at] = best;
            covered = std::max(covered, best + need.mSize);
            placed.push_back(at);
        }

        return covered;
    }
}
