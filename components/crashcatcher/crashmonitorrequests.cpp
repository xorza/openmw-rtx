#include "crashmonitorrequests.hpp"

#include <algorithm>

namespace Crash
{
    void MonitorRequests::post(const MonitorRequest& request)
    {
        {
            const std::lock_guard lock(mMutex);
            mRequests.push_back(request);
        }
        mWake.notify_one();
    }

    MonitorRequest MonitorRequests::take()
    {
        std::unique_lock lock(mMutex);
        mWake.wait(lock, [&] { return !mRequests.empty(); });
        const auto gone = std::ranges::find_if(
            mRequests, [](const MonitorRequest& one) { return !std::holds_alternative<AskToEnd>(one); });
        const auto taken = gone != mRequests.end() ? gone : mRequests.begin();
        const MonitorRequest first = *taken;
        mRequests.erase(taken);
        return first;
    }

    std::size_t MonitorRequests::takeUnasked()
    {
        const std::lock_guard lock(mMutex);
        const auto unasked = static_cast<std::size_t>(std::ranges::count_if(
            mRequests, [](const MonitorRequest& one) { return std::holds_alternative<AskToEnd>(one); }));
        mRequests.clear();
        return unasked;
    }
}
