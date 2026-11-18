#include "structurestorage.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <utility>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/requirements.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

#include "memory.hpp"

namespace Rtx
{
    namespace
    {
        std::uint32_t unitsFor(VkDeviceSize bytes)
        {
            return static_cast<std::uint32_t>(
                alignUp(bytes, StructureStorage::sAlignment) / StructureStorage::sAlignment);
        }
    }

    StructureStorage::StructureStorage(VkBufferUsageFlags usage, std::string name, const VkDeviceSize blockBytes)
        : mUsage(usage)
        , mName(std::move(name))
        , mBlockBytes(blockBytes)
    {
        assert(blockBytes % sAlignment == 0 && "a block that ends inside a unit");
    }

    Result<StructureRoom, MemoryRefusal> StructureStorage::take(const Device& device, VkDeviceSize bytes)
    {
        assert(bytes > 0);

        reclaim(device.getTimeline().getKnownFinished());
        if (mOwed.isUnowed())
        {
            mOwed = OwedGrowth(device.getMemory(), MemoryUse::Structure);
            mOwed.set(mBlockBytes);
            mMemory = &device.getMemory();
        }

        // **Every block one size, made apart from content's pool blocks**: a block sized to what a build had left
        // to place cut ranges of every size out of the blocks textures stand in, and one that went back left them as
        // holes no later structure or texture filled (`docs/architecture.md`, "A bottom level is built loose and copied
        // out tight"). Larger only for one
        // structure past it, which a block of its size holds.
        Result<StructureRoom, MemoryRefusal> taken = mBlocks.take(
            unitsFor(bytes), [](const Block&) { return true; },
            [&](const std::uint32_t units, const std::uint32_t slot) -> Result<Block, MemoryRefusal> {
                // Named only where a capture could read it: a release build names nothing, and
                // the concatenation is a trip to the heap for a name that goes nowhere.
                std::string name;
                if constexpr (sDebugNames)
                    name = mName + " " + std::to_string(slot);

                const std::uint32_t made = std::max(unitsFor(mBlockBytes), units);
                Result<Buffer, MemoryRefusal> buffer = Buffer::tryMake(MemoryUse::Structure, device,
                    BufferKind::DeviceLocal, VkDeviceSize{ made } * sAlignment, mUsage, name);
                if (!buffer.isOk())
                    return Err{ buffer.error() };

                Block block;
                block.mCapacity = made;
                block.mBuffer = std::move(buffer.value());
                mBytes += block.mBuffer.getSize();
                ++mLiveBlocks;
                return block;
            });

        if (taken.isOk())
            mLiveUnits += taken.value().mRun.mCount;
        sayFree();

        return taken;
    }

    std::optional<std::uint32_t> StructureStorage::blockToEmpty() const
    {
        if (mLiveBlocks < 2)
            return std::nullopt;

        std::optional<std::uint32_t> fewest;
        std::uint32_t fewestUsed = 0;
        std::uint32_t fewestFree = 0;
        std::uint64_t freeAll = 0;
        std::uint32_t at = 0;
        for (const Block& block : mBlocks)
        {
            if (block.mCapacity > 0)
            {
                const std::uint32_t used = block.mRuns.getUsed();
                freeAll += block.mCapacity - used;
                if (used > 0 && (!fewest.has_value() || used < fewestUsed))
                {
                    fewest = at;
                    fewestUsed = used;
                    fewestFree = block.mCapacity - used;
                }
            }
            ++at;
        }

        // The others' free room, its own left out, holds all it stands: needed, not enough, as free room in pieces
        // may not hold a structure whole, which the move then finds.
        if (!fewest.has_value() || freeAll - fewestFree < fewestUsed)
            return std::nullopt;
        return fewest;
    }

    std::optional<StructureRoom> StructureStorage::takeOutside(const VkDeviceSize bytes, const std::uint32_t avoid)
    {
        assert(bytes > 0);
        const Block* const avoided = &mBlocks.at(avoid);
        Result<StructureRoom, MemoryRefusal> taken = mBlocks.take(
            unitsFor(bytes), [&](const Block& block) { return &block != avoided; },
            [](std::uint32_t, std::uint32_t) -> Result<Block, MemoryRefusal> { return Err{ MemoryRefusal::NoRoom }; });
        if (!taken.isOk())
            return std::nullopt;
        mLiveUnits += taken.value().mRun.mCount;
        sayFree();
        return taken.value();
    }

    void StructureStorage::retire(const StructureRoom& room, const std::uint64_t until)
    {
        if (!room.empty())
            mCooling.hold(until, StructureRoom(room));
    }

    void StructureStorage::reclaim(const std::uint64_t finished)
    {
        mCooling.releaseThrough(finished, [&](const StructureRoom& room) { give(room); });
        sayFree();
    }

    StructureStorage::~StructureStorage()
    {
        if (mMemory != nullptr)
            mMemory->noteStructureFree(mFreeSaid, 0);
    }

    void StructureStorage::sayFree()
    {
        if (mMemory == nullptr)
            return;
        // What cools is counted live until it is given back: a structure a frame in flight may read stands in it.
        const VkDeviceSize live = VkDeviceSize{ mLiveUnits } * sAlignment;
        assert(mBytes >= live && "more handed out than the blocks hold");
        const VkDeviceSize free = mBytes - live;
        mMemory->noteStructureFree(mFreeSaid, free);
        mFreeSaid = free;
    }

    void StructureStorage::give(const StructureRoom& room)
    {
        if (room.empty())
            return;

        // A block that empties goes back to the device, one at a time and never a sweep, but for the last one
        // standing, which the next structure takes without asking the device. Asked only of a block that emptied,
        // and what it answers yes to is retired.
        assert(mLiveUnits >= room.mRun.mCount && "a room given back that was never counted");
        mLiveUnits -= room.mRun.mCount;
        mBlocks.give(room, [&](const Block& block) {
            if (mLiveBlocks <= 1)
                return false;

            mBytes -= block.mBuffer.getSize();
            --mLiveBlocks;
            return true;
        });
    }

    VkDeviceSize StructureStorage::getOffset(const StructureRoom& room) const
    {
        return VkDeviceSize{ room.mRun.mOffset } * sAlignment;
    }
}
