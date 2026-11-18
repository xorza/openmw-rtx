#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace Misc
{
    /// @brief Synchronize several threads
    class Barrier
    {
    public:
        /// @param count number of threads to wait on
        explicit Barrier(std::uint32_t count)
            : mThreadCount(count)
            , mRendezvousCount(0)
            , mGeneration(0)
        {
        }

        /// @brief stop execution of threads until count distinct threads reach this point
        /// @param func callable to be executed once after all threads have met
        template <class Callback>
        void wait(Callback&& func)
        {
            std::unique_lock lock(mMutex);

            ++mRendezvousCount;
            const std::uint32_t currentGeneration = mGeneration;
            if (mRendezvousCount == mThreadCount || mThreadCount == 0)
            {
                ++mGeneration;
                mRendezvousCount = 0;
                func();
                mRendezvous.notify_all();
            }
            else
            {
                mRendezvous.wait(lock, [&]() { return mGeneration != currentGeneration; });
            }
        }

    private:
        std::uint32_t mThreadCount;
        std::uint32_t mRendezvousCount;
        std::uint32_t mGeneration;
        mutable std::mutex mMutex;
        std::condition_variable mRendezvous;
    };
}
