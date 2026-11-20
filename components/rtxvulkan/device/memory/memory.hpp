#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <vulkan/vulkan_core.h>

#include <components/misc/result.hpp>
#include <components/rtx/renderer/memoryreport.hpp>

#include "largestowed.hpp"
#include "systemshare.hpp"

struct VmaAllocation_T;
struct VmaAllocator_T;
struct VmaPool_T;

namespace Rtx
{
    class Device;

    /// Why the device's memory refused an allocation: the one case content is refused for, which the
    /// caller decides the fate of (a stand-in, a mesh left out).
    enum class MemoryRefusal : std::uint8_t
    {
        NoRoom,
    };

    /// What a refusal says in a log and a report.
    constexpr std::string_view describe(const MemoryRefusal refusal)
    {
        switch (refusal)
        {
            case MemoryRefusal::NoRoom:
                break;
        }
        return "no device memory is left for it";
    }

    class MemoryAllocator;

    /// `value` rounded up to the next multiple of `alignment`.
    inline VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
    {
        return (value + alignment - 1) / alignment * alignment;
    }

    /// The memory types a request for `required` is placed in, as a mask of type indices: those whose
    /// placement — device-local, host-visible, coherent, cached — is exactly the request's, where the device
    /// has any, and otherwise every one that has what the request asks. Never AMD's device-coherent or
    /// uncached types, nor protected or lazily allocated ones, which no request here asks for.
    ///
    /// **Stated in the request rather than left to the order the driver lists its types in**: the library
    /// takes the first type that has what is asked, so staging would be write-combined system memory only
    /// where a driver lists that type before its window, and one that listed the window first would put
    /// every staging block in a window of 246 MiB. Nought where no type has what is asked.
    std::uint32_t memoryTypesFor(const VkPhysicalDeviceMemoryProperties& memory, VkMemoryPropertyFlags required);

    /// Where content — every structure and every texture — is placed: the types `memoryTypesFor`
    /// names for video memory alone, each a pool of its own, and the heap the first stands on,
    /// whose budget content's room is measured against.
    ///
    /// **Every such type, and not the first**: the library places each resource in whichever of
    /// them its requirements allow, and RADV lists a second that buffers alone take, so a structure
    /// storage's block, memory of its own (`StructureStorage`), may stand in another type than the
    /// textures in the pools.
    struct ContentTypes
    {
        std::uint32_t mTypes = 0;
        std::uint32_t mHeap = 0;
    };

    /// `ContentTypes` of a device. Throws `Unsupported` where no type is video memory alone.
    ContentTypes contentTypesOf(const VkPhysicalDeviceMemoryProperties& memory);

    /// What a range of memory is for, which says what becomes of the content where the device has
    /// no room for it — and so the order the room is given in. Each use stops where every use
    /// before it could be made again: the running mode's targets, and everything else essential
    /// once more, since a table grows by making itself again while the frame behind reads the old
    /// one.
    enum class MemoryUse : std::uint8_t
    {
        /// The frame's targets, which a change of mode makes again at its new extents. Made again
        /// only once the old are gone (`VulkanRenderer::createTargets`), so what they hold is
        /// counted once, and room is kept for the running mode's (`MemoryAllocator::reserveFrame`).
        /// Never refused here, as essential memory is not.
        Frame,

        /// What the frame cannot go without besides its targets: its tables, and the geometry every
        /// hit reads. Nothing stands in for one, so it is never refused here, and a device that
        /// refuses it is the end of the renderer.
        Essential,

        /// A mesh's bottom-level structure. Refused, the mesh is left out, and every placement of
        /// it hits nothing.
        Structure,

        /// A texture's images. Refused, its slot draws the stand-in.
        Texture,
    };

    inline constexpr std::size_t sMemoryUses = static_cast<std::size_t>(MemoryUse::Texture) + 1;

    /// The block a content type is grown by: sixty-four megabytes, against the library's quarter of a gigabyte, so the
    /// unused tail of a type's last block is a quarter of the library's. The content at the ship comes to some 6 blocks
    /// on vanilla and 57 on the PBR replacers, far under any driver's count of allocations, and none of its 1526 and
    /// 3464 allocations is past half a block (`docs/research/2026-11-14-device-layer-at-a-start.md`). Past half of
    /// one, a resource is given memory of its own.
    inline constexpr VkDeviceSize sContentBlockBytes = 64 * 1024 * 1024;

    /// What `use` is called in a message: "frame", "essential", "structure", "texture".
    std::string_view nameOf(MemoryUse use);

    /// **What a refused `take` says**: the library's answer, the bytes, the use and the resource it
    /// was for, then every heap as `describeMemory` reads them. Out of memory names its subject and
    /// how full the card was, where the call alone said nothing of either.
    std::string describeRefusedTake(
        VkResult result, VkDeviceSize bytes, MemoryUse use, std::string_view name, const MemoryReport& report);

    /// Whether `use` is memory nothing stands in for, which `MemoryAllocator::take` makes and never
    /// refuses.
    constexpr bool isNeverRefused(const MemoryUse use)
    {
        return use == MemoryUse::Frame || use == MemoryUse::Essential;
    }

    /// What `VK_EXT_memory_priority` is told of the memory a use takes: the order a driver that runs short
    /// evicts it in, lowest first. **The one statement of the priorities.** **Memory nothing stands in for
    /// stands at a half**, the library's priority for every block outside a pool, so a target big enough
    /// for an allocation of its own stands where a table in a shared block stands. **Content stands at a
    /// quarter**, one figure for structures and textures: both are what rests and is evicted by one policy
    /// (`docs/architecture.md`, "One residency policy for the content"), and neither goes before the other.
    constexpr float memoryPriorityOf(const MemoryUse use)
    {
        return isNeverRefused(use) ? 0.5f : 0.25f;
    }

    /// Where a range stands, which says whether moving what it holds gives a block back.
    enum class RangeIn : std::uint8_t
    {
        /// Memory of its own, or the library's blocks outside content's pools, where what is never refused stands.
        Elsewhere,

        /// A block of content's pools, which goes back to the device once its last range does.
        ContentBlock,
    };

    /// A range of one device allocation, and the allocator that hands it back. Not an allocation
    /// of its own: one `vkAllocateMemory` per image was 1554 for the cell the game starts in, most
    /// for a companion of two kilobytes; sub-allocated, that cell takes a couple of dozen.
    ///
    /// **Buried as it goes**, as `Owned` is: a range let go or assigned over goes to the graveyard,
    /// which gives it back once every submit made so far and the next has run, so no owner frees
    /// memory under a frame in flight by forgetting to bury it.
    class DeviceMemory
    {
    public:
        DeviceMemory() = default;
        ~DeviceMemory();

        DeviceMemory(const DeviceMemory&) = delete;
        DeviceMemory& operator=(const DeviceMemory&) = delete;

        /// Written out, because a moved-from range must stop naming the allocation. Everything else
        /// here empties itself; a range the source still described would be given back twice.
        DeviceMemory(DeviceMemory&& other) noexcept;
        DeviceMemory& operator=(DeviceMemory&& other) noexcept;

        VkDeviceMemory getHandle() const { return mHandle; }

        VkDeviceSize getOffset() const { return mOffset; }

        /// How long the range is: what letting it go gives back where the library places another.
        VkDeviceSize getSize() const { return mSize; }

        /// This range, mapped, or null where the memory is not host-visible. Mapped for as long as
        /// the range stands, and memory need not be unmapped before it is freed.
        void* map() const { return mMapped; }

        /// Whether the range stands in a block of content's pools, which its handle names, beside other ranges.
        bool standsInContentBlock() const { return mIn == RangeIn::ContentBlock; }

    private:
        friend class Graveyard;
        friend class MemoryAllocator;

        DeviceMemory(MemoryAllocator* owner, VmaAllocation_T* allocation, VkDeviceMemory handle, VkDeviceSize offset,
            void* mapped, VkDeviceSize size, std::uint32_t heap, MemoryUse use, RangeIn in);

        /// Hands the range to the graveyard, where it holds one.
        void bury();

        /// Gives the range back at once, where it holds one: the graveyard's, once nothing reads it.
        void releaseNow();

        MemoryAllocator* mOwner = nullptr;
        VmaAllocation_T* mAllocation = nullptr;
        VkDeviceMemory mHandle = VK_NULL_HANDLE;
        VkDeviceSize mOffset = 0;
        void* mMapped = nullptr;

        /// What the range counts for in `MemoryAllocator::getHeld`, taken off again as it goes.
        VkDeviceSize mSize = 0;
        std::uint32_t mHeap = 0;
        MemoryUse mUse = MemoryUse::Essential;
        RangeIn mIn = RangeIn::Elsewhere;
    };

    /// The video heap's budget for this process, which the driver states as what is free and what the process holds
    /// together, and what the process holds of the heap and keeps (`MemoryAllocator::getVideoBudget`).
    struct VideoBudget
    {
        VkDeviceSize mBudget = 0;
        VkDeviceSize mHeld = 0;
    };

    /// One content pool's room, as the library counts it: the blocks it took, what is allocated in them, and the
    /// largest free range, the largest resource it places without memory of its own.
    struct PoolRoom
    {
        std::uint32_t mType = 0;
        VkDeviceSize mBlocks = 0;
        VkDeviceSize mAllocated = 0;
        VkDeviceSize mLargestFree = 0;
    };

    /// What a content take the allocator refused for room asked, and how the heap stood: the use, the bytes, the
    /// memory type, whether it was to have memory of its own; the video heap's budget and what the process held; what
    /// each use held and the room each content use had left; and every content pool as it stood when the refusal was
    /// read (`MemoryAllocator::getLastRefused`).
    struct RefusedTake
    {
        MemoryUse mUse = MemoryUse::Texture;
        VkDeviceSize mBytes = 0;
        std::uint32_t mType = 0;
        bool mOwn = false;
        VideoBudget mVideo;
        MemoryUses mUses;
        std::array<PoolRoom, VK_MAX_MEMORY_TYPES> mPools{};
        std::uint32_t mPoolCount = 0;
    };

    /// A refused take as one log line says it.
    std::string describe(const RefusedTake& refused);

    /// The extensions of the device's memory that were enabled, which the allocator works with.
    struct MemoryExtensions
    {
        /// `VK_EXT_memory_budget`. Optional, because a driver without it only leaves the ceilings
        /// on the library's own estimate — four fifths of each heap, less what it holds itself —
        /// and the report without its budget.
        bool mBudget = false;

        /// `VK_EXT_memory_priority`. Content then goes below what the frame holds, so a driver
        /// moving video memory out under pressure moves structures and textures first; without it
        /// every allocation is one priority.
        bool mPriority = false;

        /// `VK_AMD_device_coherent_memory`, which the uncached buffer markers are placed in
        /// (`BufferKind::Markers`); nothing else is.
        bool mDeviceCoherent = false;
    };

    /// Every `vkAllocateMemory` the renderer holds, and the ranges of them nothing is using: Vulkan Memory
    /// Allocator behind a face of this fork's own, so that a buffer and an image ask for room the same way
    /// and a report reads one set of figures. Every allocation carries the device-address flag, because
    /// every buffer here may be addressed; a host-visible range is mapped as it is made. Thread-safe,
    /// because `VisibilityPass::compileEvery` reaches `take` from a thread per core.
    class MemoryAllocator
    {
    public:
        /// @param memory the device's heaps and types, read once when the device was chosen. Kept
        ///        for the report, which says what each heap is beside what it holds.
        /// @param device whose graveyard a range goes to as it is let go.
        MemoryAllocator(const Device& device, VkInstance instance, VkPhysicalDevice physicalDevice,
            const VkPhysicalDeviceMemoryProperties& memory, MemoryExtensions extensions);
        ~MemoryAllocator();

        MemoryAllocator(const MemoryAllocator&) = delete;
        MemoryAllocator& operator=(const MemoryAllocator&) = delete;

        /// Room for `buffer`, in memory that is `properties`, ready to bind, at the coarser of the
        /// driver's alignment for binding it and `alignment` — what the buffer's use owes beyond the
        /// bind, which `Buffer` reads off its usage — as `use`: the frame's targets or other
        /// essential memory, so never refused here. Throws `DeviceError` where the device has no
        /// room, naming `name`, the bytes, the use and every heap (`describeRefusedTake`), and
        /// `Unsupported` where it offers no memory type that is `properties`, which on hardware
        /// that meets the requirements means the request was wrong.
        DeviceMemory take(VkBuffer buffer, VkMemoryPropertyFlags properties, VkDeviceSize alignment, MemoryUse use,
            std::string_view name);

        /// The same for an image, at the driver's alignment, which the allocator keeps its
        /// granularity away from a buffer's, as `use`: the frame's targets or other essential
        /// memory.
        DeviceMemory take(VkImage image, VkMemoryPropertyFlags properties, MemoryUse use, std::string_view name);

        /// Video memory that `requirements` asks, which the caller binds resources into at offsets of
        /// its own — an arena's (`ImageArena`) — as `use`: the frame's targets or other essential
        /// memory, so never refused here, and throwing as `take` for a buffer does.
        DeviceMemory take(const VkMemoryRequirements& requirements, MemoryUse use, std::string_view name);

        /// Room for `buffer` as `use`, or why there is none. Out of the blocks content already holds
        /// wherever it fits there, because that costs the heap nothing; otherwise out of new memory — a
        /// block, or an allocation of its own for a resource larger than half of one — only where the heap
        /// stays below `use`'s ceiling with it. Memory nothing stands in for is `take`, which is never
        /// refused and throws instead, as does every failure but the device having no room. `name` is what
        /// `take` names, for a use never refused here.
        ///
        /// **Content never shares a block with what the frame holds.** A range of content in a block the
        /// frame's own memory opened would take room the frame had already been given, past every ceiling;
        /// in blocks of their own, what each use holds and may take are both exact to the block.
        Result<DeviceMemory, MemoryRefusal> tryTake(VkBuffer buffer, VkMemoryPropertyFlags properties,
            VkDeviceSize alignment, MemoryUse use, std::string_view name);

        /// The same for an image.
        Result<DeviceMemory, MemoryRefusal> tryTake(
            VkImage image, VkMemoryPropertyFlags properties, MemoryUse use, std::string_view name);

        /// **Room for `image` as a texture in a block content's pools hold already, other than `block`**: where a
        /// texture moved to give `block` back stands (`TextureArray::moveOut`). No new memory, so no ceiling is asked;
        /// the library tries the fullest blocks first. No room is no refusal for room, and counts as none
        /// (`getRefusedCount`): a move that finds none ends, and nothing waits on it.
        Result<DeviceMemory, MemoryRefusal> tryTakeOutside(VkImage image, VkDeviceMemory block, std::string_view name);

        /// About how many bytes of video memory `use` may still take: what content's blocks have
        /// free, and the whole blocks its ceiling leaves room for. For a caller sizing what it asks
        /// for, and not a promise: a free range may be too small for the resource that asks, so
        /// `tryTake` has the last word.
        VkDeviceSize getRoom(MemoryUse use) const;

        /// What the video heap's budget allows this process, as the driver states it and `limitBudget` holds it, and
        /// what the process holds of the heap: the driver's usage less what content's blocks have free and what is
        /// buried, both of which content takes again. What the content that rests keeps a share of the rest of
        /// (`ArrivalRoom`).
        VideoBudget getVideoBudget() const;

        /// What `use` holds on `heap`, the ranges and not the blocks around them.
        VkDeviceSize getHeld(std::uint32_t heap, MemoryUse use) const;

        /// Keeps room on the video heap for the frame's targets to be made at `bytes`, what the
        /// running mode's take: content stops where that, and not only what the targets hold now,
        /// still fits. Asked by each change of mode.
        void reserveFrame(VkDeviceSize bytes);

        /// Whether the video heap stands under the ceiling a texture is made against, as the
        /// budget and the frame's reserve set it now: whether everything content holds is what a
        /// load would stand it in. What the process holds and keeps is read, free runs and what is
        /// buried left out (`getVideoBudget`); what rests is the core's, and counted. Asks the
        /// driver's budget as the library last read it, so after `refreshBudget` for an answer
        /// about now.
        bool contentFits() const { return getContentOver() == 0; }

        /// What content holds past that ceiling, nought where it fits (`contentFits`).
        VkDeviceSize getContentOver() const;

        /// **What the process holds on the video heap past what it is held to, counting blocks as the system backs
        /// them**: the driver's usage, every block whole and its free runs with it, less the budget as `limitBudget`
        /// and the system's share hold it (`docs/architecture.md`, "Held is counted as the system backs it"). What a
        /// block emptied and given back answers, and what content's ranges never see. Nought where it fits.
        VkDeviceSize getBlocksOver() const;

        /// **Whether a block is to be given back now**: the device holds past its budget by blocks (`getBlocksOver`),
        /// or a take was refused since `refusedSeen`, which this brings up to now. A refusal under the budget is a
        /// ceiling met while blocks stand part empty, as the handheld's PBR drew the stand-in at Seyda Neen with 250
        /// MiB free in the structures' blocks. What each owner that moves its content out of a block asks, once a
        /// placement, with a count of its own.
        bool asksForBlock(std::uint64_t& refusedSeen) const;

        /// What one owner gives back soon, `blocks` in place of the `was` it said before (`BlocksComing`).
        void noteBlocksComing(std::uint32_t was, std::uint32_t blocks) noexcept;

        /// How many blocks the owners that move content out of them give back soon, together: room coming that a take
        /// refused meanwhile waits on (`whenRoomless`).
        std::uint32_t getBlocksComing() const { return mBlocksComing.load(std::memory_order_relaxed); }

        /// What `reserveFrame` was last told.
        VkDeviceSize getFrameReserve() const { return mFrameReserve; }

        /// Says the frame holds `bytes` on the video heap that something else allocated on this device
        /// — DLSS's feature, which NGX allocates — and which the driver's usage counts once made: frame
        /// memory held, so a reserve that counts it is not owed it twice. Replaces what was said before.
        void holdOutside(VkDeviceSize bytes) { mHeldOutside = bytes; }

        /// The heap video memory is taken out of, whose room `getRoom` measures.
        std::uint32_t getVideoHeap() const { return mContent.mHeap; }

        /// Whether the driver states what this process holds and may hold (`VK_EXT_memory_budget`).
        /// Asked, and not read off the report's figures: a heap the process holds nothing on is
        /// stated at nought, and a budget held to nought (`limitBudget`) reports nought.
        bool hasBudget() const { return mBudget; }

        /// Holds every heap to `bytes` where the driver states more, or to the driver's word where
        /// nothing: for a run that asks what a smaller card does, and for a test that drives
        /// content past the room. The ceilings move with it and essential memory does not, which
        /// is never refused here.
        void limitBudget(std::optional<VkDeviceSize> bytes);

        /// Has the library ask the driver its budget again, which it otherwise does only every
        /// few dozen allocations. Before a decision about what content fits, so the decision is
        /// made against what the device says now. On a device whose memory is the system's, reads
        /// what the system leaves the process as well, once a second at most (`SystemShare`).
        void refreshBudget(std::uint64_t frame);

        /// What the device claims of the host's caches, on a device whose memory is the system's: what it gave back
        /// lately (`SystemShare::getClaim`); nought on a device with memory of its own.
        VkDeviceSize getSystemClaim() const { return mShare.getClaim(); }

        /// Whether the device's memory is the system's: an integrated device, as its type states.
        bool isUnified() const { return mUnified; }

        /// What one owner's next growth of `use` takes, `bytes`, in place of the `was` it owed before
        /// (`OwedGrowth`): the largest of `use`'s kept free under the ceilings of the uses given room after it.
        void owe(MemoryUse use, VkDeviceSize was, VkDeviceSize bytes) noexcept;

        /// What `use` may take on the video heap at most, as the budget and the frame's reserve stand and the largest
        /// growth each use before it owes: what a test of the ceiling reads.
        VkDeviceSize getCeiling(MemoryUse use) const;

        /// What the structure storages hold free in their blocks, `bytes` in place of the `was` one storage said
        /// before: room only a structure takes, which the driver counts as held (`StructureStorage`).
        void noteStructureFree(VkDeviceSize was, VkDeviceSize bytes) noexcept;

        /// The largest next growth one of `use`'s owners owes on the video heap.
        VkDeviceSize getLargestOwed(MemoryUse use) const { return mOwed[static_cast<std::size_t>(use)].largest(); }

        /// How many calls to `vkAllocateMemory` stand behind everything handed out.
        std::size_t getBlockCount() const;

        /// How many ranges stand, which is what a device may not be taken apart under.
        std::size_t getLiveCount() const;

        /// Every heap of the device, what this allocator took out of each, and what the driver says
        /// is left.
        MemoryReport report() const;

        /// The last content take refused for room, its pools walked now, and how many have been: what a caller that
        /// refuses content for good reads to say why, once a hand-over, beside the refusal (`DeviceScene`).
        std::optional<RefusedTake> getLastRefused() const;
        std::uint64_t getRefusedCount() const { return mRefusedCount.load(std::memory_order_relaxed); }

    private:
        friend class DeviceMemory;

        /// The video heap's memory by use, as `report` and `noteRefused` state it.
        MemoryUses uses() const;

        /// What a resource needs of an allocation, and whether the driver binds it to nothing but an
        /// allocation of its own, which the library would otherwise find out only inside a pool that
        /// makes none.
        struct Requirements
        {
            VkMemoryRequirements mMemory{};
            bool mOwn = false;
        };

        /// Throws what `take` throws where the library answered `result` for `bytes` as `use` for
        /// `name`, and returns where it placed them.
        void checkTaken(VkResult result, VkMemoryPropertyFlags properties, VkDeviceSize bytes, MemoryUse use,
            std::string_view name) const;

        /// `Requirements` as `Query`, `vkGetBufferMemoryRequirements2` or its image's, answers `info`.
        template <auto& Query, class Info>
        Requirements requirementsOf(const Info& info) const;

        /// `tryTake`'s one path for either resource: `allocate` asks the library with the flags it
        /// is handed, which place the resource at `alignment` at least.
        template <class Allocate>
        Result<DeviceMemory, MemoryRefusal> tryAllocate(const Requirements& requirements, VkDeviceSize alignment,
            VkMemoryPropertyFlags properties, MemoryUse use, std::string_view name, Allocate&& allocate);

        /// What the library placed, as a range counted for `use`.
        DeviceMemory hold(VmaAllocation_T* allocation, VkDeviceMemory handle, VkDeviceSize offset, void* mapped,
            VkDeviceSize size, std::uint32_t type, MemoryUse use, RangeIn in);

        /// Buries `memory` in the device's graveyard.
        void retire(DeviceMemory&& memory);

        /// Frees what `memory` holds and takes it off what its use holds.
        void give(DeviceMemory& memory);

        /// How far up `heap` a use content takes may go, against the process's whole usage of it: the budget,
        /// less what the uses before `use` would take to grow, for the reason `MemoryUse` gives — on the video heap,
        /// what the frame's reserve has beyond what its targets hold, and the largest growth one owner of each owes.
        /// **One growth a use and not every owner's**: every owner's held 410 MiB of the handheld's heap from every
        /// texture and refused both a texture and a structure block under it; two growths of one use at once take the
        /// heap past its budget for a hand-over, and content gives the room back. A structure keeps a block of its own
        /// above the textures: with only the larger of the two uses' growths kept, the textures filled the heap to the
        /// structures' ceiling on the PBR replacers and left a mesh out
        /// (`docs/research/2026-11-20-handheld-open-issues.md`).
        ///
        /// **What the process holds outside the library is counted once, in the usage**: the swapchain and
        /// the driver's own memory. Taken off the budget as well, it stood twice under the ceiling, and a
        /// 7680 by 2160 swapchain of three images held two hundred megabytes of content out for nothing.
        VkDeviceSize ceilingOf(std::uint32_t heap, MemoryUse use, VkDeviceSize budget) const;

        /// What content's blocks on the video heap have free: ranges content takes again before a new block.
        VkDeviceSize getContentFree() const;

        const Device& mDevice;
        const VkPhysicalDeviceMemoryProperties& mMemory;

        /// Whether `report` may ask the driver what this process holds, rather than only counting
        /// what it asked for itself.
        bool mBudget = false;

        /// The types content is taken out of, and their heap, which is the one a use's room is
        /// measured on. Ahead of the library's allocator, because a device with none is refused
        /// before anything is made on it.
        ContentTypes mContent;

        /// The library's allocator and content's pools over it, which its destructor ends pools
        /// first and the allocator last: a member, so whatever throws after the allocator was made
        /// — a pool the library refuses — leaves nothing of either behind.
        class Library
        {
        public:
            Library(const Device& device, VkInstance instance, VkPhysicalDevice physicalDevice,
                MemoryExtensions extensions);
            ~Library();

            Library(const Library&) = delete;
            Library& operator=(const Library&) = delete;

            VmaAllocator_T* get() const { return mAllocator; }

            /// Content's blocks over memory type `type`, every block `sBlockBytes`: a size of the
            /// pool's own, so new memory is exactly a block. Null for a type content never asks for.
            VmaPool_T* poolOf(std::uint32_t type) const { return mPools[type]; }

            /// Makes the pool over `type`.
            void makePool(const Device& device, std::uint32_t type);

        private:
            VmaAllocator_T* mAllocator = nullptr;
            std::array<VmaPool_T*, VK_MAX_MEMORY_TYPES> mPools{};
        };

        Library mLibrary;

        /// What each use holds on each heap. Atomic, for the threads `take` is reached from.
        std::array<std::array<std::atomic<VkDeviceSize>, sMemoryUses>, VK_MAX_MEMORY_HEAPS> mHeld{};

        /// What each use's owners owe their next growth on the video heap, by its largest (`OwedGrowth`).
        std::array<LargestOwed, sMemoryUses> mOwed{};

        /// What the structure storages hold free in their blocks (`noteStructureFree`).
        std::atomic<VkDeviceSize> mStructureFree = 0;

        /// What the owners give back soon (`noteBlocksComing`).
        std::atomic<std::uint32_t> mBlocksComing = 0;

        std::optional<VkDeviceSize> mBudgetLimit;
        VkDeviceSize mFrameReserve = 0;

        /// Whether the device's memory is the system's, and what that leaves the device as `refreshBudget` last read
        /// it, and when: nothing on a device with memory of its own.
        bool mUnified = false;
        SystemShare mShare;
        std::optional<std::chrono::steady_clock::time_point> mSystemRead;

        /// `budget` held to `limitBudget`'s and the system's bound, whichever are less.
        VkDeviceSize heldTo(VkDeviceSize budget) const;

        /// What `holdOutside` was last told.
        VkDeviceSize mHeldOutside = 0;

        /// The last content take refused for room and the count of them, under their lock: a take is reached from
        /// several threads.
        void noteRefused(MemoryUse use, VkDeviceSize bytes, std::uint32_t type, bool own);
        mutable std::mutex mRefusedMutex;
        std::optional<RefusedTake> mLastRefused;
        std::atomic<std::uint64_t> mRefusedCount = 0;
    };
}
