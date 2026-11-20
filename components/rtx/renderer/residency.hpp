#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <components/rtx/common/deviceepoch.hpp>
#include <components/rtx/common/holdtier.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/common/rowvector.hpp>
#include <components/rtx/common/slotledger.hpp>
#include <components/rtx/scene/refusal.hpp>

#include "residencydevice.hpp"
#include "residencyop.hpp"
#include "slot.hpp"

namespace Rtx
{
    class SceneDesc;

    /// Nothing stands, and nothing is on its way.
    struct NotStanding
    {
        friend bool operator==(const NotStanding&, const NotStanding&) = default;
    };

    /// Content the renderer cannot use: terminal until the row's tenant goes.
    struct StandingFailed
    {
        Refused mCause = Refused::Texture;

        friend bool operator==(const StandingFailed&, const StandingFailed&) = default;
    };

    /// A texture's image standing in a slot, from `mLevel` down.
    struct TextureStands
    {
        std::uint8_t mLevel = 0;
        Slot mSlot;

        friend bool operator==(const TextureStands&, const TextureStands&) = default;
    };

    /// Texels asked of the feed for `mToLevel` and the levels under it to what stands, which keeps standing meanwhile
    /// (`mNow`, none for an arrival); `mArrived` once they are in hand.
    struct TextureArriving
    {
        std::uint8_t mToLevel = 0;
        FeedTicket mTicket{};
        std::optional<TextureStands> mNow{};
        bool mArrived = false;

        friend bool operator==(const TextureArriving&, const TextureArriving&) = default;
    };

    using TextureStanding = std::variant<NotStanding, TextureStands, TextureArriving, StandingFailed>;

    /// One texture row's residency, beside the scene's own row, indexed by it.
    struct TextureResident
    {
        TextureStanding mStanding;
        ImageShape mShape;

        /// The class each level stands in, from the finest down: what the plan counts.
        std::array<ClassIndex, 16> mClassAt{};

        /// The first level whose longer side is `Residency::sFloorSide` texels or fewer: what a texture that stands at
        /// all stands, and what a lowering stops at.
        std::uint8_t mFloorLevel = 0;

        /// What the row's readers ask (`TextureTable::getWant`), held: it rises at once and falls only once the coarser
        /// want has stood `Residency::sWantHoldHandOvers` (`mFallingTo`, since `mFellAt`).
        std::uint8_t mWantedLevel = 0;
        std::optional<std::uint8_t> mFallingTo;
        std::uint32_t mFellAt = 0;

        /// What the plan gives it, nothing where it drops it.
        std::optional<std::uint8_t> mPlannedLevel;

        std::uint32_t mGeneration = 0;
        HoldTier mTier = HoldTier::Unheld;

        /// The hand-over a hold last stood on it, which the cache keeps the most lately held by.
        std::uint32_t mLastHeld = 0;
        bool mLive = false;
    };

    /// A structure built loose, its tight size once the device answered it.
    struct StructureLoose
    {
        Slot mSlot;
        std::optional<std::uint64_t> mTightBytes{};

        friend bool operator==(const StructureLoose&, const StructureLoose&) = default;
    };

    /// A structure copied tight.
    struct StructureTight
    {
        Slot mSlot;

        friend bool operator==(const StructureTight&, const StructureTight&) = default;
    };

    /// A structure that refits, which stays as it was built.
    struct StructureKept
    {
        Slot mSlot;

        friend bool operator==(const StructureKept&, const StructureKept&) = default;
    };

    using StructureStanding = std::variant<NotStanding, StructureLoose, StructureTight, StructureKept, StandingFailed>;

    /// One mesh row's structure, beside the scene's own row.
    struct StructureResident
    {
        StructureStanding mStanding;
        StructureShape mShape;

        /// What the device priced its build at, and the classes it stands in loose and, once answered, tight.
        ContentPrice mLoosePrice;
        ClassIndex mLooseClass = sNoRow;
        ClassIndex mTightClass = sNoRow;
        bool mPlanned = false;
        std::uint32_t mGeneration = 0;
        HoldTier mTier = HoldTier::Unheld;
        std::uint32_t mLastHeld = 0;
        bool mLive = false;
    };

    /// What residency came to, for a report and a tour's findings.
    struct ResidencyReport
    {
        std::uint64_t mCapBytes = 0;

        /// What the plan stands in, and what of it the demanded tiers take.
        std::uint64_t mPlannedBytes = 0;
        std::uint64_t mDemandedBytes = 0;

        /// What the plan could stand nowhere, of what is demanded: a floor or a structure of `Shown` among it is the
        /// reach shrinking.
        std::uint32_t mStarvedTextures = 0;
        std::uint32_t mStarvedStructures = 0;

        /// Demanded textures planned coarser than their want, and residents whose standing is not what is planned.
        std::uint32_t mBelowWant = 0;
        std::uint32_t mWaiting = 0;

        /// This frame's work.
        std::uint32_t mOpsIssued = 0;
        std::uint64_t mBytesUploaded = 0;
        std::uint64_t mBytesCopied = 0;
        std::uint32_t mTrianglesBuilt = 0;
        std::uint32_t mTightCopies = 0;

        SlotCounts mSlots;
    };

    /// **The one owner of what content stands on the device** (`docs/architecture.md`, "Residency"): it turns what
    /// each scene's rows want into a plan that fits the device's cap, by lowering the least protected a level at a
    /// time, and only then issues operations, each with its destination reserved, under a fixed share a frame. Nothing
    /// waits for memory and nothing is refused for room. The shape of Unreal's texture streamer: the plan a pure
    /// function of its inputs, so two processes plan alike. The device and the feed are handed it at each call.
    class Residency
    {
    public:
        /// What one frame issues at most while the game runs: bytes, not milliseconds, so a frame's work is the
        /// content's and not the machine's mood. An operation is issued while any of its share is left.
        struct Shares
        {
            std::uint64_t mUploadBytes = std::uint64_t{ 16 } << 20;
            std::uint64_t mCopyBytes = std::uint64_t{ 8 } << 20;
            std::uint32_t mBuildTriangles = std::uint32_t{ 1 } << 20;
            std::uint32_t mTightCopies = 64;
        };

        /// What one scene's hand-over is of.
        struct HandOver
        {
            ResidencyDevice& mDevice;
            SceneSlot mSlot;
            const SceneDesc& mScene;
            TextureFeed& mFeed;
            Shares mShares{};
        };

        /// How long a want that fell stands before the plan takes it: about half a second of frames, so a texture
        /// passed by on a turn of the head is not lowered and raised again.
        static constexpr std::uint32_t sWantHoldHandOvers = 30;

        /// The longer side of a texture's floor.
        static constexpr std::uint32_t sFloorSide = 64;

        /// The share of what the demand leaves that what nothing demands is kept in.
        static constexpr std::uint64_t sCacheNumerator = 9;
        static constexpr std::uint64_t sCacheDenominator = 10;

        /// How many scenes stand at once: the world and the pictures inside the interface.
        static constexpr std::size_t sScenesMost = 64;

        /// @param device read for its arenas, as the layout is made.
        Residency(const ResidencyDevice& device, const SlotLayout& layout);

        Residency(const Residency&) = delete;
        Residency& operator=(const Residency&) = delete;
        ~Residency() = default;

        void addScene(SceneSlot slot);

        /// Its residents are dropped as its last hand-over rides, which `handing` is of; the device's scene goes after.
        void dropScene(const HandOver& handing);

        /// Once a frame, before any scene is handed over: what the device finished and found, its cap, and what the
        /// feed made.
        void openFrame(ResidencyDevice& device, TextureFeed& feed);

        /// Takes in what moved in the scene's rows, plans where an input moved, and hands the device the scene's
        /// operations under what is left of the frame's shares. Answers the epoch they ride.
        DeviceEpoch handOver(const HandOver& handing);

        /// Once a frame, after the last hand-over: blocks asked and given back.
        void closeFrame(ResidencyDevice& device);

        /// Whether a scene has operations queued, so its owner hands it over though nothing in it moved.
        bool owes(SceneSlot slot) const;

        /// Whether nothing is queued or on its way for a resident of `atLeast` or a more protected tier.
        bool isSettled(HoldTier atLeast) const;

        /// Whether `texture` stands from `level` or finer: what a composite asks of its layers.
        bool standsFrom(SceneSlot slot, TextureIndex texture, std::uint8_t level) const;

        const ResidencyReport& getReport() const { return mReport; }
        const TextureResident& getTexture(SceneSlot slot, TextureIndex texture) const;
        const StructureResident& getStructure(SceneSlot slot, MeshIndex mesh) const;
        const SlotLedger& getLedger() const { return mLedger; }

    private:
        /// What stood of a texture whose texels failed, given back by its scene's next hand-over.
        struct OwedDrop
        {
            TextureIndex mTexture = sNoRow;
            Slot mSlot;
        };

        struct Scene
        {
            SceneSlot mSlot;
            RowVector<TextureResident, TextureIndex> mTextures{};
            RowVector<StructureResident, MeshIndex> mStructures{};
            std::uint32_t mHandOvers = 0;

            /// A dropped scene's entry stands empty until a scene takes it again, so a scene's place, which a slot's
            /// owner names, never moves.
            bool mActive = false;
            std::vector<OwedDrop> mDropsOwed{};
        };

        /// The slots a plan stands in, a count a class, with what they come to in whole chunks and blocks kept as they
        /// move, so a step of the plan is a constant's work.
        class PlanCounts
        {
        public:
            explicit PlanCounts(const SlotLedger& ledger)
                : mLedger(&ledger)
                , mChunkBytes(ledger.getLayout().mChunkBytes)
            {
            }

            void clear();
            void add(ClassIndex of);
            void remove(ClassIndex of);
            std::uint64_t total() const;
            std::uint64_t totalIn(ArenaIndex arena) const;

            /// What of `totalIn` stands in chunks: the small slabs and the medium.
            std::uint64_t chunkBytesIn(ArenaIndex arena) const;
            std::uint32_t countOf(ClassIndex of) const;

        private:
            struct ArenaTotal
            {
                std::uint64_t mSmallSlabs = 0;
                std::uint64_t mMediumBytes = 0;
                std::uint64_t mLargeBytes = 0;
            };

            ArenaTotal& arenaOf(ClassIndex of);

            const SlotLedger* mLedger;
            std::vector<std::uint32_t> mCounts;
            std::array<ArenaTotal, 256> mArenas{};
            std::uint64_t mChunkBytes = 0;
        };

        /// A resident the plan weighs, by what it protects and costs: a lowering's victim, a starved resident, a kept
        /// one. Kept in scratch vectors the plan refills, so a plan allocates nothing once warm.
        struct Weighed
        {
            HoldTier mTier = HoldTier::Unheld;
            std::uint64_t mKey = 0;
            std::uint8_t mScene = 0;
            bool mStructure = false;
            std::uint32_t mRow = 0;
        };

        Scene& sceneOf(SceneSlot slot);
        Scene* findScene(SceneSlot slot);
        const Scene* findScene(SceneSlot slot) const;
        std::uint8_t indexOf(const Scene& scene) const;

        void syncTextures(Scene& scene, const HandOver& handing);
        void syncStructures(Scene& scene, const HandOver& handing);
        void resetTexture(Scene& scene, TextureIndex row, TextureFeed& feed);
        void resetStructure(Scene& scene, MeshIndex row);
        void plan();
        void planLowerings(std::uint64_t cap);
        void planStarving(std::uint64_t cap);
        void planCache(std::uint64_t cap);

        /// Whether `of` has a slot now; where not, a drain is asked where no block can come.
        bool ensureRoom(ClassIndex of);

        void issueDrops(Scene& scene, TextureFeed& feed);
        void issueLowerings(Scene& scene, TextureFeed& feed, const Shares& shares);
        void issueMoves(Scene& scene, const Shares& shares);
        void issueArrivals(Scene& scene, TextureFeed& feed, const Shares& shares);
        void issueStructures(Scene& scene, const Shares& shares);

        /// A slot given back by this hand-over, retired at the epoch it rides.
        void retireAfter(Slot slot);

        static SlotOwner ownerOf(std::uint8_t scene, bool structure, std::uint32_t row);

        SlotLedger mLedger;
        std::vector<Scene> mScenes;
        DeviceEvents mEvents;
        ContentBudget mBudget;
        PlanCounts mCounts;
        bool mPlanStale = true;
        std::uint32_t mNextTicket = 0;

        /// What this frame has spent of its share, and what this hand-over issues and gives back.
        Shares mSpent;
        std::vector<ResidencyOp> mOps;
        std::vector<Slot> mFreed;
        std::vector<Weighed> mWeighed;

        ResidencyReport mReport;
    };
}
