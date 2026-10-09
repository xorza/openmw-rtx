#include "joinedblocks.hpp"

#include <algorithm>
#include <cassert>
#include <span>

#include <components/rtxvulkan/device/requirements.hpp>

namespace Rtx
{
    JoinedBlocks::JoinedBlocks(const std::uint32_t blockSize, const std::uint32_t stride, const std::uint32_t ownFirst)
        : mOwnFirst(ownFirst)
        , mShared(blockSize, stride)
        , mOwn(blockSize, stride)
    {
    }

    void JoinedBlocks::open(
        const Device& device, const std::uint32_t slots, const VkBufferUsageFlags usage, const std::string_view name)
    {
        mDevice = &device;
        if constexpr (sDebugNames)
            mTableName = std::string(name) + " joined blocks";

        mShared.open(device, usage, name);
        mOwn.open(device, slots, usage, name);
        mTables.open(slots);
    }

    void JoinedBlocks::reserve(Batch& batch, const std::uint32_t shared, const std::uint32_t own)
    {
        mShared.reserve(batch, shared);
        mOwn.reserve(batch, own);

        const std::size_t sharedBlocks = mShared.getAddresses().size();
        const std::size_t ownBlocks = mOwn.at(FrameSlot{}).getAddresses().size();
        if (sharedBlocks == mJoinedShared && ownBlocks == mJoinedOwn)
            return;

        // A block past `ownFirst` would resolve two ids to one place. The ids are vertices, and the
        // vertices that fill `ownFirst` blocks carry their positions, normals and texture
        // coordinates beside these: more memory than any device holds.
        assert(sharedBlocks <= mOwnFirst && "the shared blocks reached the slots' own");

        mJoinedShared = sharedBlocks;
        mJoinedOwn = ownBlocks;
        join();
    }

    void JoinedBlocks::join()
    {
        for (std::uint32_t slot = 0; slot < mTables.count(); ++slot)
        {
            const std::span<const VkDeviceAddress> shared = mShared.getAddresses();
            const std::span<const VkDeviceAddress> own = mOwn.at(FrameSlot{ slot }).getAddresses();

            mAddresses.assign(mOwnFirst + own.size(), VkDeviceAddress{ 0 });
            std::ranges::copy(shared, mAddresses.begin());
            std::ranges::copy(own, mAddresses.begin() + mOwnFirst);

            // Made again rather than written in place, as `BlockedBuffer` makes its own: a frame in
            // flight carries the old one's address, which its burial covers.
            mTables.at(FrameSlot{ slot }) = Buffer::hostWritten(*mDevice, mAddresses.size() * sizeof(VkDeviceAddress),
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, mTableName);
            mTables.at(FrameSlot{ slot }).write(std::span<const VkDeviceAddress>(mAddresses));
        }
    }

    VkDeviceSize JoinedBlocks::getBytes() const
    {
        VkDeviceSize total = mShared.getBytes() + mOwn.getBytes();
        for (const Buffer& table : mTables.live())
            total += table.getSize();

        return total;
    }
}
