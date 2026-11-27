#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include <components/rtx/common/deviceepoch.hpp>
#include <components/rtx/common/slotledger.hpp>

#include "residencydevice.hpp"
#include "residencyop.hpp"
#include "slot.hpp"

namespace Rtx
{
    /// **A device with no card that keeps residency's contract** (`ResidencyDevice`): prices by formula, a budget a
    /// test sets, and its events a frame late, as a card's are. It asserts what a card would corrupt silently: a slot
    /// stood twice, a slot reused before the epoch it was given back at finished, and a block given back with something
    /// in it.
    class NullDevice final : public ResidencyDevice
    {
    public:
        /// The bytes a price is rounded to, an acceleration structure's alignment.
        static constexpr std::uint64_t sAlignment = 256;

        /// The longest side the device takes.
        static constexpr std::uint32_t sSideMost = 16384;

        /// @param arenas one arena for both kinds, or two.
        explicit NullDevice(std::uint64_t capBytes, std::size_t arenas = 1);

        ArenaIndex arenaOf(SlotKind kind) const override;
        std::optional<ContentPrice> priceImage(const ImageShape& shape) const override;
        ContentPrice priceStructure(const StructureShape& shape) const override;
        bool standsStructures() const override { return true; }
        ContentBudget openFrame(DeviceEvents& events) override;
        DeviceEpoch handOver(SceneSlot slot, const SceneDesc& scene, std::span<const ResidencyOp> ops) override;
        void awaitBlocks() override {}
        void acquireBlock(BlockIndex block, ArenaIndex arena, std::uint64_t bytes) override;
        void releaseBlock(BlockIndex block) override;

        void setCap(std::uint64_t capBytes) { mCapBytes = capBytes; }

        /// A scene's slot let go of or built anew: whatever it stood retires at the last epoch handed out, as a card's
        /// images go with the scene (`SceneHolder::dropViewScene`, `standScene`).
        void forgetScene(SceneSlot scene);

        /// The next block asked is refused.
        void refuseNextBlock() { mRefuseNext = true; }

        /// What a structure built at `built` bytes is answered tight at: a half, as a card's run.
        static std::uint64_t tightOf(std::uint64_t built);

        /// What it was handed, for a test to read.
        std::span<const ResidencyOp> getLastOps() const { return mLastOps; }
        std::size_t getBlocksHeld() const { return mBlocks.size(); }
        DeviceEpoch getLastEpoch() const { return DeviceEpoch{ .mValue = mEpoch }; }

    private:
        enum class SlotUse : std::uint8_t
        {
            Free,
            Live,
            Retiring,
        };

        struct SlotRecord
        {
            SlotUse mUse = SlotUse::Free;
            DeviceEpoch mRetiredAt;

            /// The scene it stands for, while it is live.
            SceneSlot mScene;
        };

        using SlotKey = std::tuple<std::uint32_t, std::uint16_t>;

        void stand(Slot slot, SceneSlot scene);
        void give(Slot slot, DeviceEpoch at);

        std::uint64_t mCapBytes;
        std::size_t mArenas;
        std::uint64_t mEpoch = 0;

        /// What the events last said finished, which nothing may be reused before, and the last epoch handed over when
        /// they said it, which the next events say finished.
        DeviceEpoch mFinished;
        DeviceEpoch mHandedAtOpen;

        std::map<SlotKey, SlotRecord> mSlots;
        std::map<std::uint32_t, std::uint64_t> mBlocks;
        std::vector<BlockIndex> mAsked;
        std::vector<BlockIndex> mRefused;
        bool mRefuseNext = false;

        /// The structures built a frame ago, answered tight by the next events.
        std::vector<TightSize> mBuilt;
        std::vector<ResidencyOp> mLastOps;
    };

    /// **A feed with no files**: every request made on the frame after it was asked, in the order asked, at most
    /// `sInFlightMost` in flight, a texture a test names failed, and one a test names taking room, which it has where
    /// the test says so and `FeedRoom` is open.
    class NullFeed final : public TextureFeed
    {
    public:
        static constexpr std::size_t sInFlightMost = 64;

        FeedAdmission ask(FeedRequest request, FeedRoom room) override;
        std::optional<FeedArrival> take() override;
        void forget(FeedTicket ticket) override;

        /// What `ask` takes from now on arrives the frame after: what a frame calls before it hands over.
        void nextFrame();

        /// Every request of this texture fails.
        void fail(TextureIndex texture) { mFailing.push_back(texture); }

        /// Every request of this texture takes room, which the feed has while `hasRoom` says so.
        void takesRoom(TextureIndex texture) { mRoomy.push_back(texture); }
        void setRoom(bool hasRoom) { mHasRoom = hasRoom; }

        std::span<const FeedRequest> getAsked() const { return mAllAsked; }

    private:
        struct InFlight
        {
            FeedRequest mRequest;
            bool mReady = false;
            bool mForgotten = false;
        };

        std::deque<InFlight> mInFlight;
        std::vector<TextureIndex> mFailing;
        std::vector<TextureIndex> mRoomy;
        bool mHasRoom = true;
        std::vector<FeedRequest> mAllAsked;
    };
}
