#include "memory.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <volk.h>

#include <components/crashcatcher/crash.hpp>
#include <components/platform/memory.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/graveyard.hpp>
#include <components/rtxvulkan/device/requirements.hpp>
#include <components/rtxvulkan/device/result.hpp>

// The one translation unit that holds the library's body. Told the two entry points every other
// function is looked up through, because no program links the loader: volk loads it, and its
// pointers are not the prototypes the header's static path would name.
#define VMA_IMPLEMENTATION
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the library reads its configuration as macros ahead of its code
#define VMA_STATIC_VULKAN_FUNCTIONS 0
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): as above
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

namespace Rtx
{
    namespace
    {
        /// A request for memory of `properties` in the types `memoryTypesFor` names on `memory`,
        /// for `use`. The priority is read only for an allocation of its own; one in a block has
        /// the block's, which `memoryPriorityOf` keeps equal to the use's.
        VmaAllocationCreateInfo askingFor(
            const VkPhysicalDeviceMemoryProperties& memory, const VkMemoryPropertyFlags properties, const MemoryUse use)
        {
            VmaAllocationCreateInfo create{};
            create.requiredFlags = properties;
            create.memoryTypeBits = memoryTypesFor(memory, properties);
            create.priority = memoryPriorityOf(use);
            if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
                create.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            return create;
        }
    }

    DeviceMemory::DeviceMemory(MemoryAllocator* const owner, VmaAllocation_T* const allocation,
        const VkDeviceMemory handle, const VkDeviceSize offset, void* const mapped, const VkDeviceSize size,
        const std::uint32_t heap, const MemoryUse use)
        : mOwner(owner)
        , mAllocation(allocation)
        , mHandle(handle)
        , mOffset(offset)
        , mMapped(mapped)
        , mSize(size)
        , mHeap(heap)
        , mUse(use)
    {
    }

    DeviceMemory::~DeviceMemory()
    {
        bury();
    }

    DeviceMemory::DeviceMemory(DeviceMemory&& other) noexcept
        : mOwner(std::exchange(other.mOwner, nullptr))
        , mAllocation(std::exchange(other.mAllocation, nullptr))
        , mHandle(std::exchange(other.mHandle, VK_NULL_HANDLE))
        , mOffset(std::exchange(other.mOffset, 0))
        , mMapped(std::exchange(other.mMapped, nullptr))
        , mSize(std::exchange(other.mSize, 0))
        , mHeap(other.mHeap)
        , mUse(other.mUse)
    {
    }

    DeviceMemory& DeviceMemory::operator=(DeviceMemory&& other) noexcept
    {
        if (this != &other)
        {
            bury();

            mOwner = std::exchange(other.mOwner, nullptr);
            mAllocation = std::exchange(other.mAllocation, nullptr);
            mHandle = std::exchange(other.mHandle, VK_NULL_HANDLE);
            mOffset = std::exchange(other.mOffset, 0);
            mMapped = std::exchange(other.mMapped, nullptr);
            mSize = std::exchange(other.mSize, 0);
            mHeap = other.mHeap;
            mUse = other.mUse;
        }

        return *this;
    }

    void DeviceMemory::bury()
    {
        if (mOwner != nullptr)
            mOwner->retire(std::move(*this));
    }

    void DeviceMemory::releaseNow()
    {
        if (mOwner != nullptr)
            std::exchange(mOwner, nullptr)->give(*this);
    }

    MemoryAllocator::Library::Library(const Device& device, const VkInstance instance,
        const VkPhysicalDevice physicalDevice, const MemoryExtensions extensions)
    {
        VmaVulkanFunctions functions{};
        functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

        // Every allocation may back a buffer that is addressed, so every one carries the flag.
        VmaAllocatorCreateInfo create{};
        create.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        if (extensions.mBudget)
            create.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        if (extensions.mPriority)
            create.flags |= VMA_ALLOCATOR_CREATE_EXT_MEMORY_PRIORITY_BIT;
        // The library leaves AMD's device-coherent types out unless told they may be used.
        if (extensions.mDeviceCoherent)
            create.flags |= VMA_ALLOCATOR_CREATE_AMD_DEVICE_COHERENT_MEMORY_BIT;
        create.physicalDevice = physicalDevice;
        create.device = device.getHandle();
        create.instance = instance;
        create.vulkanApiVersion = sApiVersion;
        create.preferredLargeHeapBlockSize = sContentBlockBytes;
        create.pVulkanFunctions = &functions;

        checkVk(device, vmaCreateAllocator(&create, &mAllocator), "vmaCreateAllocator");
    }

    MemoryAllocator::Library::~Library()
    {
        vmaDestroyAllocator(mAllocator);
    }

    MemoryAllocator::MemoryAllocator(const Device& device, const VkInstance instance,
        const VkPhysicalDevice physicalDevice, const VkPhysicalDeviceMemoryProperties& memory,
        const MemoryExtensions extensions)
        : mDevice(device)
        , mMemory(memory)
        , mBudget(extensions.mBudget)
        , mContent(contentTypesOf(memory))
        , mLibrary(device, instance, physicalDevice, extensions)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        mUnified = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    }

    std::uint32_t memoryTypesFor(const VkPhysicalDeviceMemoryProperties& memory, const VkMemoryPropertyFlags required)
    {
        constexpr VkMemoryPropertyFlags placement = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        // AMD's device-coherent types only where asked for by name, as the markers' are.
        constexpr VkMemoryPropertyFlags unasked = VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD
            | VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD | VK_MEMORY_PROPERTY_PROTECTED_BIT
            | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
        const VkMemoryPropertyFlags never = unasked & ~required;

        std::uint32_t exact = 0;
        std::uint32_t having = 0;
        for (std::uint32_t type = 0; type < memory.memoryTypeCount; ++type)
        {
            const VkMemoryPropertyFlags flags = memory.memoryTypes[type].propertyFlags;
            if ((flags & never) != 0 || (flags & required) != required)
                continue;

            having |= 1u << type;
            if ((flags & placement) == (required & placement))
                exact |= 1u << type;
        }

        return exact != 0 ? exact : having;
    }

    ContentTypes contentTypesOf(const VkPhysicalDeviceMemoryProperties& memory)
    {
        const std::uint32_t types = memoryTypesFor(memory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (types == 0)
            throw Unsupported("no memory type is video memory alone among those this device offers");

        return ContentTypes{ .mTypes = types, .mHeap = memory.memoryTypes[std::countr_zero(types)].heapIndex };
    }

    MemoryAllocator::~MemoryAllocator()
    {
        // The ranges and not the blocks: the library keeps an emptied block or two against the
        // next resource, which a range still standing in one is not.
        assert(getLiveCount() == 0 && "a device allocation was still standing a resource when the device went");
    }

    std::string_view nameOf(const MemoryUse use)
    {
        switch (use)
        {
            case MemoryUse::Frame:
                return "frame";
            case MemoryUse::Essential:
                return "essential";
            case MemoryUse::Content:
                return "content";
        }
        Crash::fatal("a memory use with no name");
    }

    std::string describeRefusedTake(const VkResult result, const VkDeviceSize bytes, const MemoryUse use,
        const std::string_view name, const MemoryReport& report)
    {
        return std::format("vmaAllocateMemory answered {} for {} bytes of {} memory for \"{}\"; video memory:\n{}",
            resultName(result), bytes, nameOf(use), name, describeMemory(report));
    }

    void MemoryAllocator::checkTaken(const VkResult result, const VkMemoryPropertyFlags properties,
        const VkDeviceSize bytes, const MemoryUse use, const std::string_view name) const
    {
        // A memory type nobody offers is a wrong request on hardware that qualifies, and everything
        // else is the device's.
        if (result == VK_ERROR_FEATURE_NOT_PRESENT)
            throw Unsupported(
                "no memory type has properties " + std::to_string(properties) + " among those this device offers");
        if (result != VK_SUCCESS)
            throw DeviceError(describeRefusedTake(result, bytes, use, name, report()));
    }

    DeviceMemory MemoryAllocator::take(const VkBuffer buffer, const VkMemoryPropertyFlags properties,
        const VkDeviceSize alignment, const MemoryUse use, const std::string_view name)
    {
        assert(use != MemoryUse::Content && "content taken as though nothing could stand in for it");
        assert(alignment > 0 && (alignment & (alignment - 1)) == 0 && "an alignment is a power of two");

        VmaAllocationCreateInfo create = askingFor(mMemory, properties, use);
        create.minAlignment = alignment;
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo placed{};
        if (const VkResult result = vmaAllocateMemoryForBuffer(mLibrary.get(), buffer, &create, &allocation, &placed);
            result != VK_SUCCESS)
            checkTaken(result, properties,
                requirementsOf<vkGetBufferMemoryRequirements2>(
                    VkBufferMemoryRequirementsInfo2{ .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
                        .pNext = nullptr,
                        .buffer = buffer })
                    .mMemory.size,
                use, name);

        return hold(
            allocation, placed.deviceMemory, placed.offset, placed.pMappedData, placed.size, placed.memoryType, use);
    }

    DeviceMemory MemoryAllocator::take(
        const VkImage image, const VkMemoryPropertyFlags properties, const MemoryUse use, const std::string_view name)
    {
        assert(use != MemoryUse::Content && "content taken as though nothing could stand in for it");

        const VmaAllocationCreateInfo create = askingFor(mMemory, properties, use);
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo placed{};
        if (const VkResult result = vmaAllocateMemoryForImage(mLibrary.get(), image, &create, &allocation, &placed);
            result != VK_SUCCESS)
            checkTaken(result, properties,
                requirementsOf<vkGetImageMemoryRequirements2>(
                    VkImageMemoryRequirementsInfo2{
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, .pNext = nullptr, .image = image })
                    .mMemory.size,
                use, name);

        return hold(
            allocation, placed.deviceMemory, placed.offset, placed.pMappedData, placed.size, placed.memoryType, use);
    }

    DeviceMemory MemoryAllocator::take(
        const VkMemoryRequirements& requirements, const MemoryUse use, const std::string_view name)
    {
        assert(use != MemoryUse::Content && "content taken as though nothing could stand in for it");

        const VmaAllocationCreateInfo create = askingFor(mMemory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, use);
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo placed{};
        checkTaken(vmaAllocateMemory(mLibrary.get(), &requirements, &create, &allocation, &placed),
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requirements.size, use, name);

        return hold(
            allocation, placed.deviceMemory, placed.offset, placed.pMappedData, placed.size, placed.memoryType, use);
    }

    std::optional<DeviceMemory> MemoryAllocator::takeBlock(const std::uint32_t type, const VkDeviceSize bytes)
    {
        assert(type < mMemory.memoryTypeCount && "a block of a memory type the device has not");
        assert((mContent.mTypes & (1u << type)) != 0 && "a block of a type content does not stand in");
        VmaAllocationCreateInfo create = askingFor(mMemory, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, MemoryUse::Content);
        create.memoryTypeBits = 1u << type;
        create.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        const VkMemoryRequirements requirements{ .size = bytes, .alignment = 1, .memoryTypeBits = 1u << type };

        VmaAllocation allocation = nullptr;
        VmaAllocationInfo placed{};
        const VkResult result = vmaAllocateMemory(mLibrary.get(), &requirements, &create, &allocation, &placed);
        if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
            return std::nullopt;
        checkTaken(result, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bytes, MemoryUse::Content, "a content block");
        return hold(allocation, placed.deviceMemory, placed.offset, placed.pMappedData, placed.size, placed.memoryType,
            MemoryUse::Content);
    }

    void MemoryAllocator::giveBlock(DeviceMemory&& block)
    {
        DeviceMemory gone = std::move(block);
        gone.releaseNow();
    }

    template <auto& Query, class Info>
    MemoryAllocator::Requirements MemoryAllocator::requirementsOf(const Info& info) const
    {
        VkMemoryDedicatedRequirements dedicated{ .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
            .pNext = nullptr,
            .prefersDedicatedAllocation = VK_FALSE,
            .requiresDedicatedAllocation = VK_FALSE };
        VkMemoryRequirements2 requirements{
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, .pNext = &dedicated, .memoryRequirements = {}
        };
        Query(mDevice.getHandle(), &info, &requirements);
        return Requirements{ .mMemory = requirements.memoryRequirements,
            .mOwn = dedicated.requiresDedicatedAllocation == VK_TRUE };
    }

    DeviceMemory MemoryAllocator::hold(VmaAllocation_T* const allocation, const VkDeviceMemory handle,
        const VkDeviceSize offset, void* const mapped, const VkDeviceSize size, const std::uint32_t type,
        const MemoryUse use)
    {
        const std::uint32_t heap = mMemory.memoryTypes[type].heapIndex;
        mHeld[heap][static_cast<std::size_t>(use)] += size;

        return DeviceMemory(this, allocation, handle, offset, mapped, size, heap, use);
    }

    void MemoryAllocator::retire(DeviceMemory&& memory)
    {
        mDevice.getGraveyard().bury(std::move(memory));
    }

    void MemoryAllocator::give(DeviceMemory& memory)
    {
        mHeld[memory.mHeap][static_cast<std::size_t>(memory.mUse)] -= memory.mSize;
        vmaFreeMemory(mLibrary.get(), memory.mAllocation);
    }

    VideoBudget MemoryAllocator::getVideoBudget() const
    {
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
        vmaGetHeapBudgets(mLibrary.get(), budgets);
        const VmaBudget& heap = budgets[mContent.mHeap];

        // **What the process has let go of is not held**: the driver counts a range buried until its frames are done,
        // which goes back once they are. Counted, a table made again would hold the arena's cap down by its last size
        // for the frames in flight.
        const Graveyard& graveyard = mDevice.getGraveyard();
        VkDeviceSize letGo = 0;
        for (std::size_t use = 0; use < sMemoryUses; ++use)
            letGo += graveyard.getBuried(static_cast<MemoryUse>(use));
        return VideoBudget{ .mBudget = heldTo(heap.budget), .mHeld = heap.usage > letGo ? heap.usage - letGo : 0 };
    }

    VkDeviceSize MemoryAllocator::getHeld(const std::uint32_t heap, const MemoryUse use) const
    {
        assert(heap < VK_MAX_MEMORY_HEAPS);
        return mHeld[heap][static_cast<std::size_t>(use)];
    }

    void MemoryAllocator::reserveFrame(const VkDeviceSize bytes)
    {
        mFrameReserve = bytes;
    }

    void MemoryAllocator::limitBudget(const std::optional<VkDeviceSize> bytes)
    {
        mBudgetLimit = bytes;
    }

    void MemoryAllocator::refreshBudget(const std::uint64_t frame)
    {
        vmaSetCurrentFrameIndex(mLibrary.get(), static_cast<std::uint32_t>(frame));
        if (!mUnified)
            return;

        // **Once a second at most**, a read of the system's figure, so no hand-over pays more than one in that time.
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (mSystemRead.has_value() && now - *mSystemRead < std::chrono::seconds(1))
            return;
        const std::optional<std::uint64_t> available = Platform::availableMemory();
        if (!available.has_value())
            return;
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
        vmaGetHeapBudgets(mLibrary.get(), budgets);
        mShare.read(budgets[mContent.mHeap].usage, *available, now - mSystemRead.value_or(now));
        mSystemRead = now;
    }

    void MemoryAllocator::owe(const VkDeviceSize was, const VkDeviceSize bytes) noexcept
    {
        mOwed.change(was, bytes);
    }

    VkDeviceSize MemoryAllocator::getOwedOutside() const
    {
        const VkDeviceSize frame = mHeld[mContent.mHeap][static_cast<std::size_t>(MemoryUse::Frame)] + mHeldOutside;
        const VkDeviceSize gap = mFrameReserve > frame ? mFrameReserve - frame : 0;
        return gap + mOwed.largest();
    }

    VkDeviceSize MemoryAllocator::heldTo(const VkDeviceSize budget) const
    {
        return std::min({ budget, mBudgetLimit.value_or(budget), mShare.getBound().value_or(budget) });
    }

    std::size_t MemoryAllocator::getLiveCount() const
    {
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
        vmaGetHeapBudgets(mLibrary.get(), budgets);

        std::size_t ranges = 0;
        for (std::uint32_t heap = 0; heap < mMemory.memoryHeapCount; ++heap)
            ranges += budgets[heap].statistics.allocationCount;

        return ranges;
    }

    std::size_t MemoryAllocator::getBlockCount() const
    {
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
        vmaGetHeapBudgets(mLibrary.get(), budgets);

        std::size_t blocks = 0;
        for (std::uint32_t heap = 0; heap < mMemory.memoryHeapCount; ++heap)
            blocks += budgets[heap].statistics.blockCount;

        return blocks;
    }

    MemoryReport MemoryAllocator::report() const
    {
        MemoryReport out;
        static_assert(
            MemoryReport::sMaxHeaps == VK_MAX_MEMORY_HEAPS, "the report's room is every heap a device states");
        out.mHeapCount = std::min<std::uint32_t>(mMemory.memoryHeapCount, MemoryReport::sMaxHeaps);

        VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
        vmaGetHeapBudgets(mLibrary.get(), budgets);

        for (std::uint32_t heap = 0; heap < out.mHeapCount; ++heap)
        {
            HeapUse& use = out.mHeaps[heap];
            use.mSize = mMemory.memoryHeaps[heap].size;
            use.mDeviceLocal = (mMemory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
            use.mReserved = budgets[heap].statistics.blockBytes;
            use.mLive = budgets[heap].statistics.allocationBytes;
            use.mBlocks = budgets[heap].statistics.blockCount;

            // Left at nought without the extension, where the library would estimate: a reader
            // tells a driver that would not say from one that said none by the columns' absence.
            if (mBudget)
            {
                use.mBudget = heap == mContent.mHeap
                    ? heldTo(budgets[heap].budget)
                    : std::min(budgets[heap].budget, mBudgetLimit.value_or(budgets[heap].budget));
                use.mHeld = budgets[heap].usage;
            }
        }

        out.mUses = uses();
        return out;
    }

    MemoryUses MemoryAllocator::uses() const
    {
        return MemoryUses{ .mFrame = getHeld(mContent.mHeap, MemoryUse::Frame) + mHeldOutside,
            .mFrameReserve = mFrameReserve,
            .mEssential = getHeld(mContent.mHeap, MemoryUse::Essential),
            .mEssentialGrowth = getLargestOwed(),
            .mContent = getHeld(mContent.mHeap, MemoryUse::Content) };
    }
}
