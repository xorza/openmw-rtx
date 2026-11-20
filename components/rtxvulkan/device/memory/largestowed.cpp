#include "largestowed.hpp"

#include <algorithm>
#include <cassert>

namespace Rtx
{
    void LargestOwed::change(const VkDeviceSize was, const VkDeviceSize bytes) noexcept
    {
        if (was == bytes)
            return;

        const std::scoped_lock lock(mMutex);
        if (was > 0)
        {
            const auto owed = std::ranges::lower_bound(mOwed, was);
            assert(owed != mOwed.end() && *owed == was && "growth forgiven that was never owed");
            mOwed.erase(owed);
        }
        if (bytes > 0)
            mOwed.insert(std::ranges::upper_bound(mOwed, bytes), bytes);
        mLargest.store(mOwed.empty() ? 0 : mOwed.back(), std::memory_order_relaxed);
    }
}
