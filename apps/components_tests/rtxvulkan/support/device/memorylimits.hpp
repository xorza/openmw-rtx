#pragma once

#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>

namespace Rtx::Testing
{
    /// What `MemoryAllocator::limitBudget` is set to for as long as one stands, and taken off
    /// after: the device is the binary's, and a limit left behind is every later test's.
    class BudgetLimit
    {
    public:
        BudgetLimit(MemoryAllocator& memory, VkDeviceSize bytes);
        ~BudgetLimit();

        BudgetLimit(const BudgetLimit&) = delete;
        BudgetLimit& operator=(const BudgetLimit&) = delete;

    private:
        MemoryAllocator& mMemory;
    };

    /// The budget at which `use`'s ceiling stands `above` over what the video heap holds now: what the heap holds, the
    /// process's memory outside the allocator among it, what the frame's reserve has beyond what its targets hold, and
    /// what the uses before `use` owe their growth. Throws on a device with no budget extension, which cannot say what
    /// the heap holds.
    VkDeviceSize budgetAbove(const MemoryAllocator& memory, MemoryUse use, VkDeviceSize above);

    /// The budget that puts the textures' ceiling at what the process holds and keeps (`MemoryAllocator::contentFits`):
    /// `budgetAbove` less the free runs and what is buried, which content's fit leaves out. A byte less, content no
    /// longer fits.
    VkDeviceSize budgetAtContent(const MemoryAllocator& memory);

    /// Video memory with no room left for content, for as long as one stands: every ceiling at nought and every gap in
    /// content's own blocks filled, so the next structure or texture is refused however small, as a card that has run
    /// out would. The frame's own memory is still made, in blocks content never shares. Lifted on the way out, the
    /// fillers before the limit.
    class NoRoomForContent
    {
    public:
        explicit NoRoomForContent(const Device& device);

    private:
        BudgetLimit mNone;
        std::vector<Buffer> mFillers;
    };
}
