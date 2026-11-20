#include "nulldevice.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <variant>

#include <components/rtx/image/textureformat.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint64_t roundUp(const std::uint64_t value, const std::uint64_t to)
        {
            return (value + to - 1) / to * to;
        }
    }

    NullDevice::NullDevice(const std::uint64_t capBytes, const std::size_t arenas)
        : mCapBytes(capBytes)
        , mArenas(arenas)
    {
        assert((arenas == 1 || arenas == 2) && "a null device of one arena or two");
    }

    ArenaIndex NullDevice::arenaOf(const SlotKind kind) const
    {
        return mArenas == 2 && kind == SlotKind::Buffer ? ArenaIndex{ 1 } : ArenaIndex{ 0 };
    }

    std::optional<ContentPrice> NullDevice::priceImage(const ImageShape& shape) const
    {
        if (shape.mFormat == TextureFormat::Unnamed || shape.mWidth > sSideMost || shape.mHeight > sSideMost
            || shape.mLevels == 0)
            return std::nullopt;
        // The texels of every level, as a card charges them before its own granule.
        const TexelLayout layout = layoutOf(shape.mFormat);
        std::uint64_t bytes = 0;
        for (std::uint8_t level = 0; level < shape.mLevels; ++level)
            bytes += layout.levelBytes(std::max(shape.mWidth >> level, 1u), std::max(shape.mHeight >> level, 1u));
        return ContentPrice{ .mBytes = roundUp(bytes, sAlignment), .mAlignment = sAlignment };
    }

    ContentPrice NullDevice::priceStructure(const StructureShape& shape) const
    {
        // A loose build at 64 bytes a triangle, a refitted one half again.
        const std::uint64_t perTriangle = shape.mRefits ? 96 : 64;
        return ContentPrice{ .mBytes = roundUp(perTriangle * shape.mTriangles + sAlignment, sAlignment),
            .mAlignment = sAlignment };
    }

    std::uint64_t NullDevice::tightOf(const std::uint64_t built)
    {
        return std::max(roundUp(built / 2, sAlignment), sAlignment);
    }

    ContentBudget NullDevice::openFrame(DeviceEvents& events)
    {
        events.clear();
        // **A frame late**: what was handed over before the last opening is done, as a card two frames in flight
        // finishes it, and what was handed over since by the next.
        mFinished = mHandedAtOpen;
        mHandedAtOpen = DeviceEpoch{ .mValue = mEpoch };
        events.mFinished = mFinished;
        for (const BlockIndex block : mAsked)
        {
            const bool refused = std::ranges::find(mRefused, block) != mRefused.end();
            (refused ? events.mBlocksRefused : events.mBlocksArrived).push_back(block);
            if (refused)
                mBlocks.erase(block.at());
        }
        mAsked.clear();
        mRefused.clear();
        events.mTightSizes = std::move(mBuilt);
        mBuilt.clear();
        for (auto& [key, record] : mSlots)
            if (record.mUse == SlotUse::Retiring && record.mRetiredAt <= mFinished)
                record.mUse = SlotUse::Free;
        return ContentBudget{ .mCapBytes = mCapBytes };
    }

    void NullDevice::stand(const Slot slot)
    {
        assert(!slot.mSlab.isNone() && "an operation into no slot");
        SlotRecord& record = mSlots[SlotKey(slot.mSlab.at(), slot.mAt)];
        assert(record.mUse != SlotUse::Live && "a slot stood twice");
        assert(record.mUse != SlotUse::Retiring && "a slot stood before the epoch it was given back at finished");
        record.mUse = SlotUse::Live;
    }

    void NullDevice::give(const Slot slot, const DeviceEpoch at)
    {
        SlotRecord& record = mSlots[SlotKey(slot.mSlab.at(), slot.mAt)];
        assert(record.mUse == SlotUse::Live && "a slot given back that stood nothing");
        record.mUse = SlotUse::Retiring;
        record.mRetiredAt = at;
    }

    DeviceEpoch NullDevice::handOver(
        const SceneSlot slot, const SceneDesc& /*scene*/, const std::span<const ResidencyOp> ops)
    {
        const DeviceEpoch epoch{ .mValue = ++mEpoch };
        mLastOps.assign(ops.begin(), ops.end());
        for (const ResidencyOp& op : ops)
            std::visit(
                [&]<class Op>(const Op& doing) {
                    if constexpr (std::is_same_v<Op, StandImage>)
                    {
                        stand(doing.mTo);
                        if (doing.mFrom.has_value())
                            give(*doing.mFrom, epoch);
                    }
                    else if constexpr (std::is_same_v<Op, DropImage> || std::is_same_v<Op, DropStructure>)
                        give(doing.mFrom, epoch);
                    else if constexpr (std::is_same_v<Op, BuildStructure>)
                    {
                        stand(doing.mLoose);
                        // A refitted structure is never made tight, and answers nothing.
                        if (doing.mBytes > 0)
                            mBuilt.push_back(TightSize{ .mScene = slot,
                                .mMesh = doing.mMesh,
                                .mGeneration = doing.mGeneration,
                                .mBytes = tightOf(doing.mBytes) });
                    }
                    else if constexpr (std::is_same_v<Op, CopyStructure>)
                    {
                        stand(doing.mTo);
                        give(doing.mFrom, epoch);
                    }
                },
                op);
        return epoch;
    }

    void NullDevice::acquireBlock(const BlockIndex block, const ArenaIndex arena, const std::uint64_t bytes)
    {
        assert(static_cast<std::size_t>(arena) < mArenas && "a block of an arena the device has not");
        assert(!mBlocks.contains(block.at()) && "a block asked twice");
        mBlocks.emplace(block.at(), bytes);
        mAsked.push_back(block);
        if (std::exchange(mRefuseNext, false))
            mRefused.push_back(block);
    }

    void NullDevice::releaseBlock(const BlockIndex block)
    {
        assert(mBlocks.contains(block.at()) && "a block given back that was never held");
        mBlocks.erase(block.at());
    }

    void NullFeed::ask(const FeedRequest& request)
    {
        assert(hasCredit() && "a request past the feed's credit");
        mInFlight.push_back(InFlight{ .mRequest = request });
        mAllAsked.push_back(request);
    }

    void NullFeed::nextFrame()
    {
        for (InFlight& flight : mInFlight)
            flight.mReady = true;
    }

    std::optional<FeedArrival> NullFeed::take()
    {
        while (!mInFlight.empty() && mInFlight.front().mReady)
        {
            const InFlight flight = mInFlight.front();
            mInFlight.pop_front();
            if (flight.mForgotten)
                continue;
            const bool fails = std::ranges::find(mFailing, flight.mRequest.mTexture) != mFailing.end();
            return FeedArrival{ .mRequest = flight.mRequest,
                .mOutcome = fails ? std::variant<FeedMade, FeedFailed>(FeedFailed{ .mCause = Refused::Texture })
                                  : std::variant<FeedMade, FeedFailed>(FeedMade{}) };
        }
        return std::nullopt;
    }

    void NullFeed::forget(const FeedTicket ticket)
    {
        for (InFlight& flight : mInFlight)
            if (flight.mRequest.mTicket == ticket)
                flight.mForgotten = true;
    }
}
