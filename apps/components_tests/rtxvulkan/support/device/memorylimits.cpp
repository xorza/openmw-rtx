#include "memorylimits.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <vulkan/vulkan_core.h>

#include <components/misc/result.hpp>
#include <components/rtx/renderer/memoryreport.hpp>

namespace Rtx::Testing
{
    BudgetLimit::BudgetLimit(MemoryAllocator& memory, const VkDeviceSize bytes)
        : mMemory(memory)
    {
        mMemory.limitBudget(bytes);
    }

    BudgetLimit::~BudgetLimit()
    {
        mMemory.limitBudget(std::nullopt);
    }

    VkDeviceSize budgetAbove(const MemoryAllocator& memory, const MemoryUse use, const VkDeviceSize above)
    {
        if (!memory.hasBudget())
            throw std::runtime_error("a device with no budget extension cannot say what the heap holds");

        const std::uint32_t heap = memory.getVideoHeap();
        const HeapUse held = memory.report().mHeaps[heap];

        VkDeviceSize owed = 0;
        const VkDeviceSize frame = memory.getHeld(heap, MemoryUse::Frame);
        if (memory.getFrameReserve() > frame)
            owed += memory.getFrameReserve() - frame;
        for (auto before = static_cast<std::size_t>(MemoryUse::Essential); before < static_cast<std::size_t>(use);
            ++before)
            owed += memory.getOwed(static_cast<MemoryUse>(before));

        return held.mHeld + owed + above;
    }

    VkDeviceSize budgetAtContent(const MemoryAllocator& memory)
    {
        const VkDeviceSize usage = memory.report().mHeaps[memory.getVideoHeap()].mHeld;
        return budgetAbove(memory, MemoryUse::Texture, 0) - (usage - memory.getVideoBudget().mHeld);
    }

    NoRoomForContent::NoRoomForContent(const Device& device)
        : mNone(device.getMemory(), 0)
    {
        // Largest first, so the gaps fill in a few dozen buffers and not thousands, down to the
        // structure alignment, below which no resource content asks for is placed.
        for (VkDeviceSize size = VkDeviceSize{ 16 } << 20; size >= 256; size /= 16)
            while (true)
            {
                Result<Buffer, MemoryRefusal> filler = Buffer::tryMake(MemoryUse::Texture, device,
                    BufferKind::DeviceLocal, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "content filler");
                if (!filler.isOk())
                    break;

                mFillers.push_back(std::move(filler.value()));
            }
    }
}
