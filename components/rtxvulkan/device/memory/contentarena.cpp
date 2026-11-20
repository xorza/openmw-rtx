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
#include <components/rtxvulkan/device/result.hpp>

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
        : mDevice(device)
        , mSupply(allocator)
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
        for (std::optional<Block>& block : mBlocks)
            if (block.has_value())
                mSupply.give(std::move(block->mMemory));
    }

    void ContentArena::acquire(const BlockIndex block, const ArenaIndex arena, const VkDeviceSize bytes)
    {
        assert((arena == arenaOf(SlotKind::Image) || arena == arenaOf(SlotKind::Buffer))
            && "a block of an arena content does not stand in");
        if (block.at() >= mBlocks.size())
        {
            mBlocks.resize(std::size_t{ block.at() } + 1);
            mBlockArenas.resize(std::size_t{ block.at() } + 1);
        }
        assert(!mBlocks[block].has_value() && "a block asked that the arena holds");
        mBlockArenas[block] = arena;
        mSupply.ask(block, static_cast<std::uint32_t>(arena), bytes);
    }

    void ContentArena::release(const BlockIndex block)
    {
        assert(mBlocks.holds(block) && mBlocks[block].has_value() && "a block given back that the arena holds not");
        mBytes -= mBlocks[block]->mMemory.getSize();
        mSupply.give(std::move(mBlocks[block]->mMemory));
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
            Block& stood = mBlocks[made.mBlock].emplace(Block{ .mMemory = std::move(*made.mMemory), .mBuffer = {} });
            // **A buffer over the whole block where structures stand in its arena**, a structure's slot a range of
            // it: making a buffer allocates nothing, so one a block costs a handle. Every block of the arena, since a
            // chunk's slabs may be of either kind where images and buffers share the arena.
            if (mBlockArenas[made.mBlock] == arenaOf(SlotKind::Buffer))
            {
                const VkBufferCreateInfo create{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .pNext = nullptr,
                    .flags = 0,
                    .size = stood.mMemory.getSize(),
                    .usage = sStructureStorageUsage,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                    .queueFamilyIndexCount = 0,
                    .pQueueFamilyIndices = nullptr,
                };
                stood.mBuffer
                    = Owned<VkBuffer, vkDestroyBuffer>::make(mDevice, vkCreateBuffer, create, "vkCreateBuffer");
                checkVk(mDevice,
                    vkBindBufferMemory(
                        mDevice.getHandle(), stood.mBuffer.get(), stood.mMemory.getHandle(), stood.mMemory.getOffset()),
                    "vkBindBufferMemory");
                mDevice.setName(stood.mBuffer.get(), "content structures");
            }
            arrived.push_back(made.mBlock);
        }
        mMade.clear();
    }

    const DeviceMemory& ContentArena::getBlock(const BlockIndex block) const
    {
        assert(mBlocks.holds(block) && "a block the arena never asked for");
        assert(mBlocks[block].has_value() && "a block that has not arrived");
        return mBlocks[block]->mMemory;
    }

    VkDeviceSize ContentArena::capUnder(const MemoryAllocator& memory) const
    {
        const VideoBudget video = memory.getVideoBudget();
        const VkDeviceSize outside = video.mHeld > mBytes ? video.mHeld - mBytes : 0;
        const VkDeviceSize spoken = outside + memory.getOwedOutside() + SlotLayout{}.mChunkBytes;
        return video.mBudget > spoken ? video.mBudget - spoken : 0;
    }

    VkBuffer ContentArena::getBuffer(const BlockIndex block) const
    {
        assert(mBlocks.holds(block) && "a block the arena never asked for");
        assert(mBlocks[block].has_value() && "a block that has not arrived");
        assert(mBlocks[block]->mBuffer.get() != VK_NULL_HANDLE && "a structure in a block of an arena of images");
        return mBlocks[block]->mBuffer.get();
    }
}
