#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <vulkan/vulkan_core.h>

#include <components/rtx/renderer/memoryreport.hpp>

#include "largestowed.hpp"

struct VmaAllocation_T;
struct VmaAllocator_T;

namespace Rtx
{
    class Device;

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

    /// Where content — every structure and every texture — may be placed: the types `memoryTypesFor` names for video
    /// memory alone, and the heap the first stands on, whose budget content's cap is measured against. **Every such
    /// type, and not the first**: RADV lists a second that buffers alone take, so the arena's structures may stand in
    /// another type than its images (`ContentArena`).
    struct ContentTypes
    {
        std::uint32_t mTypes = 0;
        std::uint32_t mHeap = 0;
    };

    /// `ContentTypes` of a device. Throws `Unsupported` where no type is video memory alone.
    ContentTypes contentTypesOf(const VkPhysicalDeviceMemoryProperties& memory);

    /// What a range of memory is for: what a report counts it as, and the priority a driver that runs short evicts it
    /// by.
    enum class MemoryUse : std::uint8_t
    {
        /// The frame's targets, which a change of mode makes again at its new extents. Made again only once the old
        /// are gone (`VulkanRenderer::createTargets`), so what they hold is counted once, and room is kept for the
        /// running mode's (`MemoryAllocator::reserveFrame`). Never refused here.
        Frame,

        /// What the frame cannot go without besides its targets: its tables, and the geometry every hit reads.
        /// Nothing stands in for one, so it is never refused here, and a device that refuses it is the end of the
        /// renderer.
        Essential,

        /// The content arena's blocks (`ContentArena`), which residency cuts into slots: what a device that has no
        /// room for one refuses (`MemoryAllocator::takeBlock`).
        Content,
    };

    inline constexpr std::size_t sMemoryUses = static_cast<std::size_t>(MemoryUse::Content) + 1;

    /// The block a content type is grown by: sixty-four megabytes, against the library's quarter of a gigabyte, so the
    /// unused tail of a type's last block is a quarter of the library's. The content at the ship comes to some 6 blocks
    /// on vanilla and 57 on the PBR replacers, far under any driver's count of allocations, and none of its 1526 and
    /// 3464 allocations is past half a block (`docs/research/2026-11-14-device-layer-at-a-start.md`). Past half of
    /// one, a resource is given memory of its own.
    inline constexpr VkDeviceSize sContentBlockBytes = 64 * 1024 * 1024;

    /// What `use` is called in a message: "frame", "essential", "content".
    std::string_view nameOf(MemoryUse use);

    /// **What a refused `take` says**: the library's answer, the bytes, the use and the resource it
    /// was for, then every heap as `describeMemory` reads them. Out of memory names its subject and
    /// how full the card was, where the call alone said nothing of either.
    std::string describeRefusedTake(
        VkResult result, VkDeviceSize bytes, MemoryUse use, std::string_view name, const MemoryReport& report);

    /// What `VK_EXT_memory_priority` is told of the memory a use takes: the order a driver that runs short evicts it
    /// in, lowest first. **The one statement of the priorities.** **Memory nothing stands in for stands at a half**,
    /// the library's priority for every block outside a pool, so a target big enough for an allocation of its own
    /// stands where a table in a shared block stands. **Content stands at a quarter**: what rests is residency's to
    /// let go of, and goes before what the frame cannot go without.
    constexpr float memoryPriorityOf(const MemoryUse use)
    {
        return use == MemoryUse::Content ? 0.25f : 0.5f;
    }

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

    private:
        friend class Graveyard;
        friend class MemoryAllocator;

        DeviceMemory(MemoryAllocator* owner, VmaAllocation_T* allocation, VkDeviceMemory handle, VkDeviceSize offset,
            void* mapped, VkDeviceSize size, std::uint32_t heap, MemoryUse use);

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
    };

    /// The video heap's budget for this process, which the driver states as what is free and what the process holds
    /// together, and what the process holds of the heap and keeps (`MemoryAllocator::getVideoBudget`).
    struct VideoBudget
    {
        VkDeviceSize mBudget = 0;
        VkDeviceSize mHeld = 0;
    };

    /// The extensions of the device's memory that were enabled, which the allocator works with.
    struct MemoryExtensions
    {
        /// `VK_EXT_memory_budget`. Optional, because a driver without it only leaves the arena's cap
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

        /// **A block of the content arena** (`ContentArena`): `bytes` of memory type `type`, an allocation of its own,
        /// at content's priority, or nothing where the device has no room for it. No ceiling is asked, since the
        /// arena's cap is the one figure content is held to. Called from the arena's thread (`ChunkSupply`).
        std::optional<DeviceMemory> takeBlock(std::uint32_t type, VkDeviceSize bytes);

        /// Frees `block` at once, which nothing stands in and nothing in flight reads: the arena's, on its thread.
        void giveBlock(DeviceMemory&& block);

        /// What the video heap's budget allows this process, as the driver states it and `limitBudget` holds it, and
        /// what the process holds of the heap: the driver's usage less what is buried, which goes back as its frames
        /// are done. What the arena's cap is read from.
        VideoBudget getVideoBudget() const;

        /// What `use` holds on `heap`, the ranges and not the blocks around them.
        VkDeviceSize getHeld(std::uint32_t heap, MemoryUse use) const;

        /// Keeps room on the video heap for the frame's targets to be made at `bytes`, what the running mode's take:
        /// the arena's cap stands where that, and not only what the targets hold now, still fits (`getOwedOutside`).
        /// Asked by each change of mode.
        void reserveFrame(VkDeviceSize bytes);

        /// What `reserveFrame` was last told.
        VkDeviceSize getFrameReserve() const { return mFrameReserve; }

        /// **What the heap owes beyond what it holds, outside the content arena**: what the frame's reserve has beyond
        /// what its targets and what the upscaler's runtime holds, and the largest growth one essential table owes —
        /// the `gap` and the `owed` of the arena's cap (`docs/architecture.md`, "The arena's cap").
        VkDeviceSize getOwedOutside() const;

        /// Says the frame holds `bytes` on the video heap that something else allocated on this device
        /// — DLSS's feature, which NGX allocates — and which the driver's usage counts once made: frame
        /// memory held, so a reserve that counts it is not owed it twice. Replaces what was said before.
        void holdOutside(VkDeviceSize bytes) { mHeldOutside = bytes; }

        /// The heap video memory is taken out of, whose budget the arena's cap is read against.
        std::uint32_t getVideoHeap() const { return mContent.mHeap; }

        /// The memory types content stands in (`ContentTypes::mTypes`).
        std::uint32_t getContentTypes() const { return mContent.mTypes; }

        /// Whether the driver states what this process holds and may hold (`VK_EXT_memory_budget`).
        /// Asked, and not read off the report's figures: a heap the process holds nothing on is
        /// stated at nought, and a budget held to nought (`limitBudget`) reports nought.
        bool hasBudget() const { return mBudget; }

        /// Holds every heap to `bytes` where the driver states more, or to the driver's word where nothing: for a run
        /// that asks what a smaller card does, and for a test that drives content past the room. The arena's cap moves
        /// with it and essential memory does not, which is never refused here.
        void limitBudget(std::optional<VkDeviceSize> bytes);

        /// Has the library ask the driver its budget again, which it otherwise does only every
        /// few dozen allocations. Before a decision about what content fits, so the decision is
        /// made against what the device says now.
        void refreshBudget(std::uint64_t frame);

        /// What the device holds of its content's heap where that heap is the system's memory, nothing on a device with
        /// memory of its own: what the host's sampler takes the device's share from (`Resource::MemoryArbiter`).
        std::optional<VkDeviceSize> getSystemHeld() const;

        /// What the host's sampler holds the device to, on a device whose memory is the system's
        /// (`Resource::SystemShare`); nothing leaves it to its driver's word.
        void setSystemBound(std::optional<VkDeviceSize> bound) { mSystemBound = bound; }

        /// Whether the device's memory is the system's: an integrated device, as its type states.
        bool isUnified() const { return mUnified; }

        /// What one essential owner's next growth takes, `bytes`, in place of the `was` it owed before (`OwedGrowth`):
        /// the largest is kept free under the arena's cap (`getOwedOutside`).
        void owe(VkDeviceSize was, VkDeviceSize bytes) noexcept;

        /// The largest next growth one essential owner owes on the video heap.
        VkDeviceSize getLargestOwed() const { return mOwed.largest(); }

        /// How many calls to `vkAllocateMemory` stand behind everything handed out.
        std::size_t getBlockCount() const;

        /// How many ranges stand, which is what a device may not be taken apart under.
        std::size_t getLiveCount() const;

        /// Every heap of the device, what this allocator took out of each, and what the driver says
        /// is left.
        MemoryReport report() const;

    private:
        friend class DeviceMemory;

        /// The video heap's memory by use, as `report` states it.
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

        /// What the library placed, as a range counted for `use`.
        DeviceMemory hold(VmaAllocation_T* allocation, VkDeviceMemory handle, VkDeviceSize offset, void* mapped,
            VkDeviceSize size, std::uint32_t type, MemoryUse use);

        /// Buries `memory` in the device's graveyard.
        void retire(DeviceMemory&& memory);

        /// Frees what `memory` holds and takes it off what its use holds.
        void give(DeviceMemory& memory);

        const Device& mDevice;
        const VkPhysicalDeviceMemoryProperties& mMemory;

        /// Whether `report` may ask the driver what this process holds, rather than only counting
        /// what it asked for itself.
        bool mBudget = false;

        /// The types content is taken out of, and their heap, which the arena's cap is measured on. Ahead of the
        /// library's allocator, because a device with none is refused before anything is made on it.
        ContentTypes mContent;

        /// The library's allocator, ended last: a member, so whatever throws after it was made leaves nothing behind.
        class Library
        {
        public:
            Library(const Device& device, VkInstance instance, VkPhysicalDevice physicalDevice,
                MemoryExtensions extensions);
            ~Library();

            Library(const Library&) = delete;
            Library& operator=(const Library&) = delete;

            VmaAllocator_T* get() const { return mAllocator; }

        private:
            VmaAllocator_T* mAllocator = nullptr;
        };

        Library mLibrary;

        /// What each use holds on each heap. Atomic, for the threads `take` is reached from.
        std::array<std::array<std::atomic<VkDeviceSize>, sMemoryUses>, VK_MAX_MEMORY_HEAPS> mHeld{};

        /// What the essential owners owe their next growth on the video heap, by its largest (`OwedGrowth`).
        LargestOwed mOwed;

        std::optional<VkDeviceSize> mBudgetLimit;
        VkDeviceSize mFrameReserve = 0;

        /// Whether the device's memory is the system's, and what the host's sampler holds it to then.
        bool mUnified = false;
        std::optional<VkDeviceSize> mSystemBound;

        /// `budget` held to `limitBudget`'s and the system's bound, whichever are less.
        VkDeviceSize heldTo(VkDeviceSize budget) const;

        /// What `holdOutside` was last told.
        VkDeviceSize mHeldOutside = 0;
    };
}
