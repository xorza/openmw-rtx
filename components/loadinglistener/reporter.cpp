#include "reporter.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>

#include "loadinglistener.hpp"

namespace Loading
{
    void Reporter::addTotal(std::size_t value)
    {
        const std::scoped_lock lock(mMutex);
        mTotal += value;
        mUpdated.notify_all();
    }

    void Reporter::addProgress(std::size_t value)
    {
        const std::scoped_lock lock(mMutex);
        mProgress += value;
        mUpdated.notify_all();
    }

    void Reporter::complete()
    {
        const std::scoped_lock lock(mMutex);
        mDone = true;
        mUpdated.notify_all();
    }

    void Reporter::wait(Listener& listener) const
    {
        std::unique_lock lock(mMutex);
        while (!mDone)
        {
            listener.setProgressRange(mTotal);
            listener.setProgress(mProgress);
            mUpdated.wait(lock);
        }
    }
}
