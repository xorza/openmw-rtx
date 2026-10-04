#include "bouncereservoirs.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

#include <components/rtx/frame/bouncepairing.hpp>
#include <components/rtx/shaders/bouncereuse.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>

namespace Rtx
{
    namespace
    {
        constexpr VkBufferUsageFlags sUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        /// Both pairing textures for a frame `height` traced rows tall. **The steps' deviation is the
        /// one whose mean distance a uniform disc of the reuse's radius has** (ReSTIR PT Enhanced
        /// §7): `σ √(π/2) = 2R/3`, so `σ = √(8 / 9π) R`, held where the shuffles still spread it.
        std::vector<std::uint32_t> pairingsFor(std::uint32_t height)
        {
            const float radius
                = std::max(Shaders::BOUNCE_RADIUS_SHARE * static_cast<float>(height), Shaders::BOUNCE_RADIUS_LEAST);
            const float deviation = std::clamp(std::sqrt(8.0f / (9.0f * std::numbers::pi_v<float>)) * radius, 0.8f,
                static_cast<float>(Shaders::BOUNCE_PAIRING_SIZE_1) / 6.0f);

            const BouncePairing first(Shaders::BOUNCE_PAIRING_SIZE_0, deviation, 0);
            const BouncePairing second(Shaders::BOUNCE_PAIRING_SIZE_1, deviation, 1);
            std::vector<std::uint32_t> steps(first.getSteps().begin(), first.getSteps().end());
            steps.insert(steps.end(), second.getSteps().begin(), second.getSteps().end());
            return steps;
        }
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
        mPaired = Buffer::deviceLocal(mDevice, pixels * sizeof(std::uint32_t), sUsage, "bounce-paired");

        if (reuses)
        {
            Batch batch(mDevice.getPool());
            const std::vector<std::uint32_t> steps = pairingsFor(height);
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
