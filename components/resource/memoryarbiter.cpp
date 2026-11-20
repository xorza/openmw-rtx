#include "memoryarbiter.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>

#include "cachebudget.hpp"

namespace Resource
{
    MemoryArbiter::MemoryArbiter(CacheBudget& budget)
        : mBudget(budget)
    {
    }

    void MemoryArbiter::postDeviceHeld(const std::optional<std::uint64_t> held)
    {
        const std::scoped_lock lock(mMutex);
        mDeviceHeld = held;
    }

    std::optional<std::uint64_t> MemoryArbiter::getDeviceBound() const
    {
        const std::scoped_lock lock(mMutex);
        return mShare.getBound();
    }

    std::optional<std::uint64_t> MemoryArbiter::getAvailable() const
    {
        const std::scoped_lock lock(mMutex);
        return mAvailable;
    }

    void MemoryArbiter::destroyEvicted()
    {
        mBudget.destroyEvicted();
    }

    void MemoryArbiter::noteRead(const std::chrono::nanoseconds took)
    {
        const std::scoped_lock lock(mMutex);
        mSlowestRead = std::max(mSlowestRead, took);
    }

    std::chrono::nanoseconds MemoryArbiter::takeSlowestRead()
    {
        const std::scoped_lock lock(mMutex);
        return std::exchange(mSlowestRead, std::chrono::nanoseconds{});
    }

    void MemoryArbiter::take(const SystemReading& reading)
    {
        std::uint64_t claim = 0;
        {
            const std::scoped_lock lock(mMutex);
            // The share follows its readings over time, so it reads on every tick, the device's arrivals or none: a
            // claim that only arrivals read stood withheld from the caches for good once they stopped.
            if (mDeviceHeld.has_value() && reading.mAvailable.has_value())
                mShare.read(*mDeviceHeld, *reading.mAvailable, reading.mAt - mLastAt.value_or(reading.mAt));
            claim = mShare.getClaim();
            mAvailable = reading.mAvailable;
            mLastAt = reading.mAt;
        }
        mBudget.yieldTo(claim);
        mBudget.setBytes(CacheBudget::derive(reading.mAvailable, mBudget.getHeld(), reading.mHeapFree));
    }
}
