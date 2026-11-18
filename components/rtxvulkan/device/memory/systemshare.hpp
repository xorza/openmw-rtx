#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

#include <vulkan/vulkan_core.h>

namespace Rtx
{
    /// **The device's share of the system's memory, on a device whose memory is the system's**: what it is held to, and
    /// what it claims of the host's caches (`docs/architecture.md`, "On a unified device the device and the caches
    /// split the system's memory"). Both follow their readings over `sFollow`, as an exponential average, so a host's
    /// spike costs the device nothing and a lasting growth takes its room back within the minute; and a block the
    /// device gave back stays claimed while the average of what it held stands above it, so the caches take it only
    /// once the device has gone without it for about as long.
    class SystemShare
    {
    public:
        /// How long a reading takes to be followed: the average moves `1 - 1/e` of the way to a reading held this
        /// long.
        static constexpr std::chrono::seconds sFollow{ 60 };

        /// What the host's own side keeps of the system's memory, half the caches' own reserve: as the device and the
        /// caches meet, the caches stop first.
        static constexpr VkDeviceSize sHostReserveBytes = VkDeviceSize{ 1 } << 29;

        /// **What the device may hold, read now**: what it holds, `held`, and what the system leaves the process beside
        /// it, `available`, less `sHostReserveBytes`. A driver's budget for such a heap is the heap less the device's
        /// own use, and sees nothing the process's heap and the other programs hold (RADV,
        /// `radv_get_memory_budget_properties`).
        static constexpr VkDeviceSize boundOf(const VkDeviceSize held, const VkDeviceSize available)
        {
            const VkDeviceSize room = held + available;
            return room - std::min(room, sHostReserveBytes);
        }

        /// Takes a reading: the device holds `held` and the system leaves the process `available`, `elapsed` after the
        /// last reading. The first reading is taken whole.
        void read(const VkDeviceSize held, const VkDeviceSize available, const std::chrono::duration<double> elapsed)
        {
            const double follow = 1.0 - std::exp(-elapsed.count() / std::chrono::duration<double>(sFollow).count());
            const auto toward = [follow](const std::optional<double>& average, const double reading) {
                return average.has_value() ? *average + (reading - *average) * follow : reading;
            };
            mBound = toward(mBound, static_cast<double>(boundOf(held, available)));
            mHeldAverage = toward(mHeldAverage, static_cast<double>(held));
            mHeld = held;
        }

        /// What the device is held to: the average of its bound. Nothing before the first reading.
        std::optional<VkDeviceSize> getBound() const
        {
            return mBound.has_value() ? std::optional<VkDeviceSize>(static_cast<VkDeviceSize>(*mBound)) : std::nullopt;
        }

        /// What the device claims of the caches: how far the average of what it held stands above what it holds,
        /// what it gave back lately; nought where it holds more than its average, and before the first reading.
        VkDeviceSize getClaim() const
        {
            if (!mHeldAverage.has_value())
                return 0;
            const double over = *mHeldAverage - static_cast<double>(mHeld);
            return over > 0.0 ? static_cast<VkDeviceSize>(over) : 0;
        }

    private:
        std::optional<double> mBound;
        std::optional<double> mHeldAverage;
        VkDeviceSize mHeld = 0;
    };
}
