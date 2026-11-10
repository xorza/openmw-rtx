#include "thread.hpp"

#include <condition_variable>
#include <mutex>

namespace Platform
{
    bool sleepUnlessStopped(const std::stop_token& stop, const std::chrono::nanoseconds period)
    {
        std::mutex mutex;
        std::condition_variable_any woken;
        std::unique_lock lock(mutex);
        woken.wait_for(lock, stop, period, [] { return false; });
        return !stop.stop_requested();
    }
}
