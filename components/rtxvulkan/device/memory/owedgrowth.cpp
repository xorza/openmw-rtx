#include "owedgrowth.hpp"

namespace Rtx
{
    void OwedGrowth::set(const VkDeviceSize bytes) noexcept
    {
        // Owed to nothing, an owner outside the video heap owes nothing, whatever it grows to.
        if (mMemory == nullptr || bytes == mBytes)
            return;
        mMemory->owe(mUse, mBytes, bytes);
        mBytes = bytes;
    }
}
