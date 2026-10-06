#include "bouncereservoirs.hpp"

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#include <components/rtx/frame/bouncepairing.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/shaders/shared/bouncereuse.h>

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

        mWidth = width;
        mHeight = height;
        mReuses = reuses;
        allocate(mReuses && mDemanded);
    }

    void BounceReservoirs::demand()
    {
        assert(mReuses && "a reuse asked of reservoirs made for a chain that never reuses");
        if (mDemanded)
            return;

        // A chain not yet sized makes them at its first resize.
        mDemanded = true;
        if (mWidth > 0)
            allocate(true);
    }

    void BounceReservoirs::allocate(const bool full)
    {
        mFull = full;
        mStride = full ? mWidth : 1;
        const VkDeviceSize pixels = full ? VkDeviceSize{ mWidth } * mHeight : 1;

        mReservoirs
            = Buffer::deviceLocal(mDevice, pixels * sizeof(Shaders::GpuBounceReservoir), sUsage, "bounce-reservoirs");
        mHistory = Buffer::deviceLocal(mDevice, pixels * sizeof(Shaders::GpuBounceReservoir), sUsage, "bounce-history");
        for (std::size_t half = 0; half < 2; ++half)
            mOrigins[half] = Buffer::deviceLocal(mDevice, pixels * sizeof(Shaders::GpuBounceOrigin), sUsage,
                half == 0 ? "bounce-origins-0" : "bounce-origins-1");
        mThrough = Buffer::deviceLocal(mDevice, pixels * sizeof(std::uint32_t), sUsage, "bounce-through");
        mPaired = Buffer::deviceLocal(mDevice, pixels * sizeof(std::uint32_t), sUsage, "bounce-paired");

        if (full)
        {
            Batch batch(mDevice.getPool());
            const std::vector<std::uint32_t> steps = bouncePairingSteps(mHeight);
            mPairing = uploadBuffer(batch, std::span<const std::uint32_t>(steps), sUsage, "bounce-pairing");
            batch.flush();
        }
        else
            mPairing = Buffer::deviceLocal(mDevice, sizeof(std::uint32_t), sUsage, "bounce-pairing");

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
