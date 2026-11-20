#pragma once

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/device.hpp>
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

    /// The budget at which the content arena's cap stands at `cap` (`ContentArena::capUnder`), as the device holds
    /// what it holds now. Throws on a device with no budget extension, which cannot say what the heap holds.
    VkDeviceSize budgetForCap(const Device& device, VkDeviceSize cap);
}
