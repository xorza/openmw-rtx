#include "contentarena.hpp"

#include <bit>
#include <cassert>
#include <cstdint>
#include <format>
#include <optional>
#include <utility>
#include <vector>

#include <components/rtx/image/textureformat.hpp>
#include <components/rtxvulkan/device/device.hpp>

#include "buffer.hpp"
#include "bufferusage.hpp"
#include "formats.hpp"
#include "image.hpp"

namespace Rtx
{
    namespace
    {
        /// The lowest of `types` that is video memory alone, as content stands in: the arena a kind of resource's
        /// requirements allow.
        ArenaIndex arenaAmong(const std::uint32_t types, const std::uint32_t content)
        {
            const std::uint32_t allowed = types & content;
            if (allowed == 0)
                throw Unsupported(
                    std::format("no memory type of video memory alone takes the content's resources: "
                                "the resource takes types {:#x}, video memory alone is {:#x}",
                        types, content));
            return static_cast<ArenaIndex>(std::countr_zero(allowed));
        }
    }

    ContentArena::ContentArena(const Device& device, MemoryAllocator& allocator)
        : mSupply(allocator)
    {
        // **Every colour image of one tiling asks the same types**, as Vulkan states of `memoryTypeBits`; one
        // texture's image stands for every one. A description the device takes: a compressed image asking for storage
        // with no second format is no valid image, which AMD's Windows driver answered with no types at all.
        const std::uint32_t content = allocator.getContentTypes();
        const VkMemoryRequirements image = Image::requirementsFor(device,
            ImageDescription{ .mWidth = 256,
                .mHeight = 256,
                .mFormat = toVulkanFormat(TextureFormat::Bc7Srgb),
                .mUsage = sUploadedTextureUsage,
                .mMipLevels = 9 });
        const VkMemoryRequirements buffer = Buffer::requirementsFor(device, sContentBlockBytes, sStructureStorageUsage);
        mArenaOf[static_cast<std::size_t>(SlotKind::Image)] = arenaAmong(image.memoryTypeBits, content);
        mArenaOf[static_cast<std::size_t>(SlotKind::Buffer)] = arenaAmong(buffer.memoryTypeBits, content);
        mMade.reserve(ChunkSupply::sInFlightMost);
    }

    ContentArena::~ContentArena()
    {
        for (std::optional<DeviceMemory>& block : mBlocks)
            if (block.has_value())
                mSupply.give(std::move(*block));
    }

    void ContentArena::acquire(const BlockIndex block, const ArenaIndex arena, const VkDeviceSize bytes)
    {
        assert((arena == arenaOf(SlotKind::Image) || arena == arenaOf(SlotKind::Buffer))
            && "a block of an arena content does not stand in");
        if (block.at() >= mBlocks.size())
            mBlocks.resize(std::size_t{ block.at() } + 1);
        assert(!mBlocks[block].has_value() && "a block asked that the arena holds");
        mSupply.ask(block, static_cast<std::uint32_t>(arena), bytes);
    }

    void ContentArena::release(const BlockIndex block)
    {
        assert(mBlocks.holds(block) && mBlocks[block].has_value() && "a block given back that the arena holds not");
        mBytes -= mBlocks[block]->getSize();
        mSupply.give(std::move(*mBlocks[block]));
        mBlocks[block].reset();
    }

    void ContentArena::collect(std::vector<BlockIndex>& arrived, std::vector<BlockIndex>& refused)
    {
        mSupply.take(mMade);
        for (BlockMade& made : mMade)
        {
            if (!made.mMemory.has_value())
            {
                refused.push_back(made.mBlock);
                continue;
            }
            mBytes += made.mMemory->getSize();
            mBlocks[made.mBlock] = std::move(made.mMemory);
            arrived.push_back(made.mBlock);
        }
        mMade.clear();
    }

    const DeviceMemory& ContentArena::getBlock(const BlockIndex block) const
    {
        assert(mBlocks.holds(block) && "a block the arena never asked for");
        assert(mBlocks[block].has_value() && "a block that has not arrived");
        return *mBlocks[block];
    }
}
