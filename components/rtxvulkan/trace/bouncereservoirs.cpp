#include "bouncereservoirs.hpp"

#include <cassert>
#include <cstdint>

#include <components/rtx/shaders/bouncereuse.h>

namespace Rtx
{
    namespace
    {
        constexpr VkBufferUsageFlags sUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }

    BounceReservoirs::BounceReservoirs(const Device& device)
        : mDevice(device)
    {
    }

    void BounceReservoirs::resize(const std::uint32_t width, const std::uint32_t height, const bool reuses)
    {
        assert(width > 0 && height > 0);

        mStride = reuses ? width : 1;
        const VkDeviceSize pixels = reuses ? VkDeviceSize{ width } * height : 1;

        mReservoirs
            = Buffer::deviceLocal(mDevice, pixels * sizeof(Shaders::GpuBounceReservoir), sUsage, "bounce-reservoirs");
        mHistory = Buffer::deviceLocal(mDevice, pixels * sizeof(Shaders::GpuBounceReservoir), sUsage, "bounce-history");
        for (std::size_t half = 0; half < 2; ++half)
            mOrigins[half] = Buffer::deviceLocal(mDevice, pixels * sizeof(Shaders::GpuBounceOrigin), sUsage,
                half == 0 ? "bounce-origins-0" : "bounce-origins-1");
        mThrough = Buffer::deviceLocal(mDevice, pixels * sizeof(std::uint32_t), sUsage, "bounce-through");

        reset();
    }

    bool BounceReservoirs::turn(const bool reuses)
    {
        const bool history = mHistoryKept && reuses;
        mNow = 1 - mNow;
        mHistoryKept = reuses;
        return history;
    }
}
