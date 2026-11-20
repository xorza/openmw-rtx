#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/common/index.hpp>
#include <components/rtx/common/rowvector.hpp>
#include <components/rtx/common/slotledger.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/renderer/residencydevice.hpp>
#include <components/rtx/renderer/residencyop.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/renderer/structuretightness.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/accelerationstructure.hpp>
#include <components/rtxvulkan/device/memory/blockedbuffer.hpp>
#include <components/rtxvulkan/device/memory/growablebuffer.hpp>

#include "structurebuild.hpp"

namespace Rtx
{
    class Batch;
    class ContentArena;
    class Device;
    class GpuTimer;
    class SceneDesc;

    /// A list consumed from the front in the order it was filled, its consumed front let go of at
    /// each `settle`. **Bounded by what is outstanding**: emptied only once drained, a backlog that
    /// was asked something every frame never drained, and grew by every structure built, with a
    /// reallocation on whichever frame crossed its capacity.
    template <class T>
    struct Backlog
    {
        std::vector<T> mItems;
        std::size_t mRead = 0;

        std::size_t size() const { return mItems.size() - mRead; }
        bool empty() const { return size() == 0; }
        const T& at(std::size_t offset) const { return mItems[mRead + offset]; }
        void push(const T& item) { mItems.push_back(item); }
        void pop(std::size_t count) { mRead += count; }

        /// Lets go of what was consumed, moving what is left to the front: between two passes, so
        /// nothing reads a place across it.
        void settle()
        {
            mItems.erase(mItems.begin(), mItems.begin() + static_cast<std::ptrdiff_t>(mRead));
            mRead = 0;
        }
    };

    /// How much scratch one run of builds works in, past what one mesh needs alone: a cell's meshes in a run or two.
    inline constexpr VkDeviceSize sStructureBuildRoom = 64 * 1024 * 1024;

    /// Whether a bottom level is built once or refit as its mesh deforms, which its build flags say.
    enum class StructureMotion : std::uint8_t
    {
        Still,
        Refit,
    };

    /// **One bottom-level acceleration structure per mesh, in the slot residency hands it** (`BuildStructure`,
    /// `CopyStructure`, `DropStructure`): a structure is built loose into its slot, the driver is asked what a tight
    /// copy would come to, which residency is told as an event (`TightSize`), and the copy residency then asks for is
    /// made into the slot it names. What stands where, and when, is residency's; this records the commands and keeps
    /// the handles. A structure built to be refitted keeps its slack, so it is built where it stays.
    class BottomLevelStore
    {
    public:
        /// What a bottom level of `motion` is built with, its first build and every refit or rebuild alike: an
        /// update is asked of the flags of the build that allowed one.
        static VkBuildAccelerationStructureFlagsKHR buildFlagsOf(StructureMotion motion);

        /// What a structure of `shape` takes as built, the driver's answer for its counts, at the offset alignment
        /// a structure's range starts at (`ResidencyDevice::priceStructure`).
        static ContentPrice price(const Device& device, const StructureShape& shape);

        /// @param buildRoom what one run of builds may work in: `sStructureBuildRoom`, and less for a test that
        ///        splits a few meshes into runs.
        explicit BottomLevelStore(const Device& device, VkDeviceSize buildRoom = sStructureBuildRoom);

        /// What `execute` records into and reads from.
        struct Executing
        {
            Batch& mBatch;
            const ContentArena& mArena;

            /// The first copy of the deforming vertices, which a deforming mesh's structure is built over; the static
            /// vertices `SceneBuffers` keeps for every hit, which a static mesh's structure is built over; and the
            /// shared index blocks, which every structure is built through.
            const BlockedBuffer& mPoses;
            const BlockedBuffer& mPositions;
            const BlockedBuffer& mIndices;

            /// Which posed placement brings these, on the clock the rebuild rota reads.
            std::uint64_t mPlacement = 0;
            GpuTimer* mTimer = nullptr;
        };

        /// **Does what `ops` name of structures**, in their slots: drops, then copies, then builds, each built loose
        /// structure asked what it would come to tight. The meshes whose structures were made, moved or dropped join
        /// `getChanged`, whose placements the caller writes again.
        void execute(const Executing& executing, const SceneDesc& scene, std::span<const ResidencyOp> ops);

        /// Destroys the structures of `meshes`, which the scene freed. Idempotent: a mesh residency dropped already
        /// holds nothing.
        void release(std::span<const MeshIndex> meshes);

        /// **Appends the tight size of every structure whose question has run**, as `scene`'s, and forgets each: what
        /// residency copies a structure tight by. Read once the host has waited past the submit that carried the
        /// question, so which frame answers is a function of the frames and not of the clock.
        void readAnswers(SceneSlot scene, std::vector<TightSize>& into);

        /// The meshes `execute` made, moved or dropped a structure of since the last `clearChanged`.
        std::span<const MeshIndex> getChanged() const { return mChanged.getSlots(); }
        void clearChanged() { mChanged.clear(); }

        /// A row for every mesh slot of a scene of `meshes`, which never shrinks: a smaller table would drop the
        /// handles above its end and leak their structures.
        void grow(std::size_t meshes);

        std::size_t size() const { return mRows.size(); }

        /// Whether `mesh` is a row this has been handed, built or not.
        bool holds(const MeshIndex mesh) const { return mRows.holds(mesh); }

        /// Whether `mesh` has a structure: not where its slot is free, where it holds no triangle, or where
        /// residency stands none of it.
        bool stands(const MeshIndex mesh) const { return mRows.holds(mesh) && !mRows[mesh].mStructure.isEmpty(); }
        VkAccelerationStructureKHR getStructure(const MeshIndex mesh) const
        {
            return mRows[mesh].mStructure.getHandle();
        }
        /// Nought where none stands.
        VkDeviceAddress getAddress(const MeshIndex mesh) const { return mRows[mesh].mStructure.getAddress(); }

        /// Whether `mesh`'s structure was built with `ALLOW_UPDATE`, which is whether the scene's mesh deformed at
        /// the time it was built — `MeshRange::deforms`. False where no structure stands.
        bool isUpdatable(const MeshIndex mesh) const { return mRows[mesh].mUpdatable; }

        /// What a refit of every structure that can be refit works in at once, each at the aligned end of the one
        /// before, and the rota's pick in a whole build's room where an update's was: what the refit's scratch is
        /// grown to as the refittable arrive, so no frame grows it.
        VkDeviceSize getRefitScratchBytes() const;

        /// What `mesh`'s place in a refit works in: a whole build's scratch for a rebuild from nothing, an update's
        /// for the rest, as the store asked the driver when it was built.
        VkDeviceSize getScratchFor(const MeshIndex mesh, const VkBuildAccelerationStructureModeKHR mode) const
        {
            return mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR ? mRows[mesh].mBuildScratch
                                                                          : mRows[mesh].mUpdateScratch;
        }

        /// **The rota**: of the posed meshes, the one built whole longest ago, which `placement` builds whole
        /// again, or nothing where each was built whole on `placement` itself. A refit keeps the tree the first pose
        /// was built over and moves its boxes, which fit a body met crouched badly once it stands, so every ray
        /// through it pays for as long as the body lives; one a placement, so no frame carries two and every frame
        /// carries the same (a threshold made frames alternate between one and none). Noted as built at
        /// `placement`.
        MeshIndex pickRebuild(std::span<const MeshIndex> posed, std::uint64_t placement);

        /// What the standing structures take, each at what it was built or copied at.
        VkDeviceSize getLiveBytes() const { return mLiveBytes; }

        // Read by the tests and by nothing else.
        VkDeviceSize getScratchBytes() const { return mScratch.get().getSize(); }

        /// What the structures answered and not yet copied tight would come to, and what they occupy now: the pair
        /// says what compaction has left to give back. The answers already read, and not a question of its own:
        /// `VK_QUERY_RESULT_WAIT_BIT` would stand the CPU still on the frame a cell arrives in.
        VkDeviceSize getCompactableBytes() const { return mCompactableTight; }
        VkDeviceSize getCompactableNowBytes() const { return mCompactableNow; }

        /// What every structure answered so far came to tight against as first built, by its triangles.
        const StructureTightness& getTightness() const { return mTightness; }

        /// What the structures cloned out of a draining slab came to, since the store was made.
        VkDeviceSize getMovedOutBytes() const { return mMovedOut; }

    private:
        /// Where a structure stands in its compaction: built to be refitted, slack and all; built loose and not yet
        /// asked about; its question in a batch the queue may not have reached; answered, residency told; copied
        /// tight.
        enum class Compaction : std::uint8_t
        {
            Kept,
            Loose,
            Asked,
            Answered,
            Tight,
        };

        /// One question recorded, in the order they were, so the ones ready to read are a prefix.
        /// `mAt` is the timeline value the batch it was recorded into rides.
        struct Ask
        {
            MeshIndex mSlot = sNoRow;
            std::uint64_t mAt = 0;
        };

        /// One mesh slot: its structure in the slot residency named, and what the refit and the compaction know about
        /// it. One row and not six lists, so a slot cannot be half updated.
        struct Row
        {
            AccelerationStructure mStructure;

            /// The slot it stands in and the tenant it was built for, which an operation names.
            Slot mSlot{};
            std::uint32_t mGeneration = 0;

            /// What it was built or copied at, what its question answered, and its triangles.
            VkDeviceSize mSize = 0;
            VkDeviceSize mTightSize = 0;
            std::uint32_t mTriangles = 0;

            /// What a refit asks for, and what a build from nothing asks for, kept so a frame does not ask the driver
            /// again. The first is nought for a mesh not built to be refitted.
            VkDeviceSize mUpdateScratch = 0;
            VkDeviceSize mBuildScratch = 0;

            /// The placement that last built the structure whole: its arrival's, and then the rota's.
            std::uint64_t mRebuiltAt = 0;

            /// Whether the structure was built with `ALLOW_UPDATE`.
            bool mUpdatable = false;

            Compaction mCompaction = Compaction::Kept;
            std::uint64_t mAskedAt = 0;
        };

        void drop(const DropStructure& op);
        void copy(VkCommandBuffer commands, const ContentArena& arena, const CopyStructure& op);
        void build(const Executing& executing, const SceneDesc& scene);

        /// Records the compaction question for every loose structure not yet asked about.
        void askWhatCompactionWouldSave(VkCommandBuffer commands);

        /// Records the question for the run of consecutive slots gathered in `mAskScratch`, which
        /// starts at `first`, and empties it. Nothing where nothing was gathered.
        void askRun(VkCommandBuffer commands, std::uint32_t first);

        /// Whether a tight copy of the structure is smaller than as built: what is counted as compaction's saving.
        static bool shrinks(const Row& row) { return row.mTightSize > 0 && row.mTightSize < row.mSize; }

        /// Whether `ask` is still the question its slot is waiting on: a slot built again since is
        /// waiting on a later one.
        bool isOutstanding(const Ask& ask) const
        {
            const Row& row = mRows[ask.mSlot];
            return row.mCompaction == Compaction::Asked && row.mAskedAt == ask.mAt;
        }

        /// Buries `slot`'s structure and forgets what the compaction knew about it, ahead of the slot being built
        /// again or given back. Idempotent: a slot holding nothing buries nothing.
        void retire(MeshIndex slot);

        const Device& mDevice;
        const VkDeviceSize mBuildRoom;

        /// One row per mesh slot, grown with the mesh table.
        RowVector<Row, MeshIndex> mRows;

        /// What a run of builds works in. Kept across builds rather than made per arrival, settling at
        /// the largest run — the build room, or one mesh past it — and grown by a replacement that
        /// buries the old: a build in flight still reads it, and the next build's work is ordered
        /// after it by the barrier every command buffer opens with.
        GrowableBuffer mScratch;

        /// What one run of builds describes.
        StructureBuildBatch mBuild;

        /// One build of a hand-over: how big its structure comes out, how much scratch its build works in, and where
        /// in its run's scratch that lands. Filled in passes over the list and read in the last, so they are one row.
        struct BuildRow
        {
            VkDeviceSize mSize = 0;
            VkDeviceSize mScratch = 0;
            VkDeviceSize mScratchOffset = 0;
        };

        /// Refilled per hand-over: the builds its operations name, and one row each in that order.
        std::vector<BuildStructure> mBuilds;
        std::vector<BuildRow> mBuilding;

        /// Where each run of builds ends in `mBuilding`, the last at its end.
        std::vector<std::size_t> mRunEnds;

        /// The builds actually recorded in a run.
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> mLiveBuilds;

        /// One query per slot, grown with the mesh table. The pool it outgrows is buried and not
        /// destroyed — a batch in flight may still be writing into it — and whoever was asked
        /// through it is asked again through the new one.
        QueryPool mCompactable;
        std::uint32_t mCompactablePool = 0;

        /// The refittable meshes, kept as each slot is built and retired.
        SlotSet<MeshIndex> mRefittable;

        /// The slots built loose and not yet asked about, which is the whole of what the next question names.
        std::vector<MeshIndex> mLoose;

        /// The questions outstanding, oldest first.
        Backlog<Ask> mAsked;

        /// One run of consecutive slots' handles, and one run's answers. Refilled per run.
        std::vector<VkAccelerationStructureKHR> mAskScratch;
        std::vector<VkDeviceSize> mReadScratch;

        /// The meshes whose structures were made, moved or dropped (`getChanged`).
        SlotSet<MeshIndex> mChanged;

        VkDeviceSize mLiveBytes = 0;
        VkDeviceSize mCompactableNow = 0;
        VkDeviceSize mCompactableTight = 0;
        StructureTightness mTightness;
        VkDeviceSize mMovedOut = 0;
    };
}
