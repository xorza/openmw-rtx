#include "memorylimits.hpp"

#include <optional>
#include <stdexcept>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/contentarena.hpp>

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

    VkDeviceSize budgetForCap(const Device& device, const VkDeviceSize cap)
    {
        MemoryAllocator& memory = device.getMemory();
        if (!memory.hasBudget())
            throw std::runtime_error("a device with no budget extension cannot say what the heap holds");

        // The cap is the budget less what stands outside the arena and what that owes, which no budget moves: read
        // under the driver's own budget, where the cap stands above nought, it says what a budget for `cap` is.
        memory.limitBudget(std::nullopt);
        const VkDeviceSize budget = memory.getVideoBudget().mBudget;
        const VkDeviceSize under = device.getContent().capUnder(memory);
        if (under == 0)
            throw std::runtime_error("the device's own budget leaves the arena nothing, so no budget names a cap");
        return cap + (budget - under);
    }
}
