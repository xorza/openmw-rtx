#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/common/index.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/accelerationstructure.hpp>
#include <components/rtxvulkan/device/memory/blockedbuffer.hpp>
#include <components/rtxvulkan/device/memory/bufferusage.hpp>
#include <components/rtxvulkan/device/memory/growablebuffer.hpp>
#include <components/rtxvulkan/device/memory/structurestorage.hpp>

#include "structurebuild.hpp"

namespace Rtx
{
    class Batch;
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

    /// When `BottomLevelStore::build` copies its structures out of the loose storage.
    enum class BuildCompaction : std::uint8_t
    {
        /// Over the placements after it, a budget each (`BottomLevelStore::prepareCompaction`), as
        /// a cell streams in and the frame may not wait.
        Streamed,

        /// Run by run before it returns, each run built, waited on, copied and waited on again, as
        /// NVIDIA's load builds them (`nvvk::RaytracingBuilderKHR::buildBlas`): for a load, which
        /// waits anyway. What is made after it then stands against the structures' tight size, and
        /// the loose structures never stand more than a run at once.
        Settled,
    };

    /// How much scratch one run of builds works in, and how many positions it stages, past what one
    /// mesh needs alone: a storage block's worth, which takes a cell's meshes in a run or two.
    inline constexpr VkDeviceSize sStructureBuildRoom = 64 * 1024 * 1024;

    /// One bottom-level acceleration structure per mesh, inside storage buffers at offsets, and the
    /// compaction that keeps them tight: a structure is built loose, the driver is asked what a tight
    /// copy would come to, and the copy is made in the storage that keeps it.
    ///
    /// The loose structures stand in storage of their own, which every one of them leaves, so each
    /// block empties whole once its structures are copied out, as RTXMU gives a build's memory
    /// back. A copy taking room in the block its loose structure left filled the holes of every
    /// block (206 MiB kept for 66 MiB of tight structures once), since a structure never copied
    /// held each block. A structure built to be refitted keeps its slack, so it is built where it stays.
    class BottomLevelStore
    {
    public:
        /// @param buildRoom what one run of `build` may work in: `sStructureBuildRoom`, and less for
        ///        a test that splits a few meshes into runs.
        explicit BottomLevelStore(const Device& device, VkDeviceSize buildRoom = sStructureBuildRoom);

        /// Creates and records the build of a structure for each of `meshes`, taking storage for it.
        /// A slot that already holds one has it destroyed first: a slot the scene handed out again
        /// carries different geometry. A mesh the device has no room for is left out and appended to
        /// `refused`: its slot holds no structure, so every placement of it names none, which the
        /// top level skips (`stands` says which).
        ///
        /// @param poses the first copy of the deforming vertices, which is what a deforming mesh's
        ///        structure is built over — `SkinPass` has written the pose into it.
        /// @param positions the static vertices `SceneBuffers` keeps for every hit, which a static
        ///        mesh's structure is built over, with no copy staged for the build alone.
        /// @param indices the shared index blocks, which every structure is built through.
        /// @param placement which posed placement brings these, on the clock the rebuild rota reads:
        ///        what `getRebuiltAt` answers for each of `meshes` until the rota comes round.
        /// @param compaction when the structures are copied out of the loose storage. Settled
        ///        flushes `batch` after each run, and what the caller recorded into it ahead runs
        ///        with the first.
        void build(Batch& batch, const SceneDesc& scene, std::span<const Index> meshes, const BlockedBuffer& poses,
            const BlockedBuffer& positions, const BlockedBuffer& indices, std::uint64_t placement,
            BuildCompaction compaction, std::vector<Refusal>& refused);

        /// Destroys the structures of `meshes` and gives their storage back. Idempotent, because
        /// both the frame that places and the one that appends run it. The last frame's top level
        /// still names them, which their burial covers.
        void release(std::span<const Index> meshes);

        std::size_t size() const { return mRows.size(); }

        /// Whether the device had no room for a structure since the store was made, which a store
        /// made against more room might have built. Kept past the mesh that went without, which
        /// may since have gone: an answer that asks for a build too often and never too seldom.
        bool wasRefusedRoom() const { return mRefusedRoom; }

        /// Whether `mesh` has a structure: not where its slot is free, where it holds no triangle,
        /// or where the device had no room for it.
        bool stands(const Index mesh) const { return !mRows[mesh].mStructure.isEmpty(); }
        VkAccelerationStructureKHR getStructure(const Index mesh) const { return mRows[mesh].mStructure.getHandle(); }
        VkDeviceAddress getAddress(const Index mesh) const { return mRows[mesh].mStructure.getAddress(); }

        /// Whether `mesh`'s structure was built with `ALLOW_UPDATE`, which is whether the scene's
        /// mesh deformed at the time it was built — `MeshRange::deforms`. Whether a mesh deforms is
        /// fixed when it arrives, so this is also whether the mesh can ever be refitted.
        bool isUpdatable(const Index mesh) const { return mRows[mesh].mUpdatable; }

        /// Every mesh that `stands` and `isUpdatable`: a few dozen bodies among thousands of
        /// statics, which is what sizing the refit's scratch reads rather than every slot.
        std::span<const Index> getRefittable() const { return mRefittable.getSlots(); }

        /// What a refit of `mesh` asks for, so a frame does not have to ask the driver again.
        /// Nought for a mesh that was not built to be refitted.
        VkDeviceSize getUpdateScratch(const Index mesh) const { return mRows[mesh].mUpdateScratch; }

        /// What a build of `mesh` from nothing asks for, which a rebuild into the same room reads.
        VkDeviceSize getBuildScratch(const Index mesh) const { return mRows[mesh].mBuildScratch; }

        /// Which placement last built `mesh` whole rather than refitting it — its arrival's, until
        /// the rota comes round — and the rota's own note of a rebuild.
        std::uint64_t getRebuiltAt(const Index mesh) const { return mRows[mesh].mRebuiltAt; }
        void noteRebuilt(const Index mesh, const std::uint64_t placement) { mRows[mesh].mRebuiltAt = placement; }

        /// Reads every compaction answer whose placement has certainly run, and makes a tight
        /// structure for as many of the answered as this placement's budget takes. The set it
        /// returns names the meshes whose structures moved, whose rows the caller writes again.
        /// Counts the placement, which is what the readiness rule below reads; one call per
        /// placement.
        const SlotSet& prepareCompaction();

        /// Copies each structure `prepareCompaction` made room for into it.
        void recordCompaction(VkCommandBuffer commands, GpuTimer* timer);

        /// The room the structures were given, and what they occupy in it. Neither counts the
        /// geometry they were built from.
        VkDeviceSize getBytes() const { return mStorage.getBytes() + mLooseStorage.getBytes(); }
        VkDeviceSize getLiveBytes() const { return mStorage.getLiveBytes() + mLooseStorage.getLiveBytes(); }

        // Read by the tests and by nothing else.
        VkDeviceSize getScratchBytes() const { return mScratch.get().getSize(); }
        VkDeviceSize getLooseBytes() const { return mLooseStorage.getBytes(); }

        /// What the structures still to be copied tight would come to, or nought where there are
        /// none and where the device would not say — what is left to save, falling to nothing over
        /// the placements after an arrival. The answers already read, and not a question of its
        /// own: `VK_QUERY_RESULT_WAIT_BIT` would stand the CPU still on the frame a cell arrives in.
        VkDeviceSize getCompactableBytes() const { return mCompactableTight; }

        /// What those same structures occupy now. The pair says what compaction has left to give
        /// back.
        VkDeviceSize getCompactableNowBytes() const { return mCompactableNow; }

    private:
        /// What the compaction knows about the structure in a slot — a state per slot and a
        /// question per structure, asked once, because asking about every loose structure at every
        /// build never read an answer while cells kept arriving.
        enum class Tightness : std::uint8_t
        {
            /// No structure, or one that refits and so was built where it stays, slack and all.
            None,

            /// Built loose and not yet asked about.
            Loose,

            /// Its question is in a batch the queue may not have reached.
            Asked,

            /// The driver's answer is in `mTightSize`, and the copy is owed.
            Answered,

            /// Copied out of the loose storage, tight where a tight copy is smaller: nothing more
            /// to do.
            Tight,
        };

        /// One question recorded, in the order they were, so the ones ready to read are a prefix.
        /// `mAt` is the timeline value the batch it was recorded into rides.
        struct Ask
        {
            Index mSlot = sNoIndex;
            std::uint64_t mAt = 0;
        };

        /// What the compaction knows about the structure in one slot: where it stands, the timeline
        /// value its question rides — what `readAnswers` reads it against — what it was created at,
        /// and what the driver said a tight copy would come to, once answered.
        struct Compaction
        {
            Tightness mTightness = Tightness::None;
            std::uint64_t mAskedAt = 0;
            VkDeviceSize mBuiltSize = 0;
            VkDeviceSize mTightSize = 0;
        };

        /// Records the compaction question for every loose structure not yet asked about — `mLoose`,
        /// and whoever a replaced pool loses the answer of.
        void askWhatCompactionWouldSave(VkCommandBuffer commands);

        /// Records the question for the run of consecutive slots gathered in `mAskScratch`, which
        /// starts at `first`, and empties it. Nothing where nothing was gathered.
        void askRun(VkCommandBuffer commands, std::uint32_t first);

        /// Reads every answer whose submit the timeline says has run.
        void readAnswers();

        /// Makes a room in the kept storage for as many answered structures as `budget` takes, and
        /// queues the copy of each into its room for `recordCompaction`; the meshes it moved join
        /// `mMovedMeshes`. A new block is made at what is left of the budget.
        void copyAnswered(VkDeviceSize budget);

        /// What every answered structure's copy would take: a settled run's budget, which then
        /// copies them all into blocks of exactly that.
        VkDeviceSize getAnsweredBytes() const;

        /// Hands out the rooms of both storages whose stamps the timeline passed.
        void reclaim();

        /// Whether a tight copy of the structure `state` answered for is smaller than the structure:
        /// what is copied tight and counted as a saving. One that is not, or that the driver gave
        /// no size for, is copied as it stands, so it leaves the loose storage all the same.
        static bool shrinks(const Compaction& state)
        {
            return state.mTightSize > 0 && state.mTightSize < state.mBuiltSize;
        }

        /// Whether `ask` is still the question its slot is waiting on: a slot built again since is
        /// waiting on a later one.
        bool isOutstanding(const Ask& ask) const
        {
            const Compaction& state = mRows[ask.mSlot].mCompaction;
            return state.mTightness == Tightness::Asked && state.mAskedAt == ask.mAt;
        }

        /// Buries `slot`'s structure and forgets what the compaction knew about it, ahead of the
        /// slot being built again or given back. Idempotent: a slot holding nothing buries nothing.
        void retire(Index slot);

        /// One mesh slot: its structure, in its room, and what the refit and the compaction know
        /// about it. One row and not six lists, so a slot cannot be half updated.
        struct Row
        {
            AccelerationStructure mStructure;

            /// What a refit asks for, and what a build from nothing asks for, kept so a frame does
            /// not ask the driver again. The first is nought for a mesh not built to be refitted.
            VkDeviceSize mUpdateScratch = 0;
            VkDeviceSize mBuildScratch = 0;

            /// The placement that last built the structure whole: its arrival's, and then the
            /// rota's.
            std::uint64_t mRebuiltAt = 0;

            /// Whether the structure was built with `ALLOW_UPDATE`.
            bool mUpdatable = false;

            Compaction mCompaction;
        };

        const Device& mDevice;
        const VkDeviceSize mBuildRoom;

        // Before the rows, which give their rooms back to them as they go. The structures that
        // stay — copied tight, or built to be refitted — and the loose ones until they are copied.
        StructureStorage mStorage{ sStructureStorageUsage, "bottom level structures",
            StructureStorage::LastBlock::Kept };
        StructureStorage mLooseStorage{ sStructureStorageUsage, "loose bottom level structures",
            StructureStorage::LastBlock::GivenBack };

        /// One row per mesh slot, grown with the mesh table.
        std::vector<Row> mRows;

        /// What a run of builds works in. Kept across builds rather than made per arrival, settling at
        /// the largest run — the build room, or one mesh past it — and grown by a replacement that
        /// buries the old: a build in flight still reads it, and the next build's work is ordered
        /// after it by the barrier every command buffer opens with.
        GrowableBuffer mScratch;

        /// What one run of `build` describes.
        StructureBuildBatch mBuild;

        /// One mesh of the list `build` was handed: how big its structure comes out, how much scratch
        /// its build works in, and where in its run's scratch that lands. Filled in passes over the
        /// list and read in the last, so they are one row.
        struct BuildRow
        {
            VkDeviceSize mSize = 0;
            VkDeviceSize mScratch = 0;
            VkDeviceSize mScratchOffset = 0;
        };

        /// Refilled per `build`, one row per mesh handed in, in that order.
        std::vector<BuildRow> mBuilding;

        /// Where each run of `build` ends in `mBuilding`, the last at its end.
        std::vector<std::size_t> mRunEnds;

        /// The builds actually recorded, which is `mBuild.mBuilds` without the meshes that came out
        /// at nought bytes — a mesh with no triangles is described by nobody and built by nobody.
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> mLiveBuilds;

        /// One query per slot, grown with the mesh table. The pool it outgrows is buried and not
        /// destroyed — a batch in flight may still be writing into it — and whoever was asked
        /// through it is asked again through the new one.
        QueryPool mCompactable;
        std::uint32_t mCompactablePool = 0;

        /// What `getRefittable` answers, kept as each slot is built and retired.
        SlotSet mRefittable;

        /// The slots built loose and not yet asked about, which is the whole of what the next
        /// question names: a walk of every slot for them missed the cache a slot, on every arrival.
        std::vector<Index> mLoose;

        /// The questions outstanding, oldest first, and the slots answered and not yet copied.
        Backlog<Ask> mAsked;
        Backlog<Index> mAnswered;

        /// One run of consecutive slots' handles, and one run's answers. Refilled per run.
        std::vector<VkAccelerationStructureKHR> mAskScratch;
        std::vector<VkDeviceSize> mReadScratch;

        /// What the answered structures occupy as they stand, and what they would come to tight,
        /// so the pair the report prints is a saving rather than a number on its own.
        VkDeviceSize mCompactableNow = 0;
        VkDeviceSize mCompactableTight = 0;

        bool mRefusedRoom = false;

        /// What this placement copies, refilled each time. Kept so a compaction allocates nothing.
        std::vector<VkCopyAccelerationStructureInfoKHR> mCompactionCopies;

        /// The meshes those copies moved, whose placements the caller writes again.
        SlotSet mMovedMeshes;
    };
}
