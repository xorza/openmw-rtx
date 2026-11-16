#include "structurestorage.hpp"

#include <algorithm>
#include <cassert>
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

        return taken;
    }

    void StructureStorage::retire(const StructureRoom& room, const std::uint64_t until)
    {
        if (!room.empty())
            mCooling.hold(until, StructureRoom(room));
    }

    void StructureStorage::reclaim(const std::uint64_t finished)
    {
        mCooling.releaseThrough(finished, [&](const StructureRoom& room) { give(room); });
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
