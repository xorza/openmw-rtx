#pragma once

#include <utility>

#include <vulkan/vulkan_core.h>

#include "memory.hpp"

namespace Rtx
{
    /// **What one essential owner's next growth will take of the video heap**, owed to the allocator for as long as the
    /// owner stands: a table made again at twice its size beside the old one, a blocked buffer's next block. The
    /// arena's cap keeps room for the largest (`MemoryAllocator::getOwedOutside`), not for every owner's or a second
    /// copy of everything it holds.
    class OwedGrowth
    {
    public:
        /// Owes nothing, to nothing: what an owner that is not opened holds.
        OwedGrowth() = default;

        explicit OwedGrowth(MemoryAllocator& memory)
            : mMemory(&memory)
        {
        }

        ~OwedGrowth() { set(0); }

        OwedGrowth(OwedGrowth&& other) noexcept
            : mMemory(other.mMemory)
            , mBytes(std::exchange(other.mBytes, 0))
        {
        }

        OwedGrowth& operator=(OwedGrowth&& other) noexcept
        {
            if (this == &other)
                return *this;
            set(0);
            mMemory = other.mMemory;
            mBytes = std::exchange(other.mBytes, 0);
            return *this;
        }

        OwedGrowth(const OwedGrowth&) = delete;
        OwedGrowth& operator=(const OwedGrowth&) = delete;

        /// Owes `bytes` from now on, in place of what was owed before; nothing where it owes to no allocator.
        void set(VkDeviceSize bytes) noexcept;

        /// Whether it owes to no allocator, as made with nothing.
        bool isUnowed() const { return mMemory == nullptr; }

    private:
        MemoryAllocator* mMemory = nullptr;
        VkDeviceSize mBytes = 0;
    };
}
