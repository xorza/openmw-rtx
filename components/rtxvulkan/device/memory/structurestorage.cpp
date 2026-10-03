#include "structurestorage.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>
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

    StructureStorage::StructureStorage(VkBufferUsageFlags usage, std::string name)
        : mUsage(usage)
        , mName(std::move(name))
    {
    }

    Misc::Result<StructureRoom, std::string_view> StructureStorage::take(
        const Device& device, VkDeviceSize bytes, VkDeviceSize least)
    {
        assert(bytes > 0);

        reclaim(device.getTimeline().getKnownFinished());

        // Every live block may hold a structure, so the list is asked for the first that fits,
        // and a new block is as large as the caller asked or as the structure needs — as the
        // structure needs and no more, where the device has no room for what the caller asked.
        Misc::Result<StructureRoom, std::string_view> taken = mBlocks.take(
            unitsFor(bytes), [](const Block&) { return true; },
            [&](const std::uint32_t units, const std::uint32_t slot) -> Misc::Result<Block, std::string_view> {
                // Named only where a capture could read it: a release build names nothing, and
                // the concatenation is a trip to the heap for a name that goes nowhere.
                std::string name;
                if constexpr (sDebugNames)
                    name = mName + " " + std::to_string(slot);

                const auto make = [&](const std::uint32_t capacity) {
                    return Buffer::tryMake(MemoryUse::Structure, device, BufferKind::DeviceLocal,
                        VkDeviceSize{ capacity } * sAlignment, mUsage, name);
                };

                std::uint32_t made = std::max(unitsFor(least), units);
                Misc::Result<Buffer, std::string_view> buffer = make(made);
                if (!buffer.isOk() && made > units)
                {
                    made = units;
                    buffer = make(made);
                }

                if (!buffer.isOk())
                    return Misc::Err{ buffer.error() };

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

        // A block that empties goes back to the device, one at a time and never a sweep. A
        // refitted structure stays for the life of its mesh and pins its block. The last one
        // standing stays, so a scene that empties and fills does not ask for it back on the next
        // arrival. Asked only of a block that emptied, and what it answers yes to is retired.
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
