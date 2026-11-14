#pragma once

#include <memory>
#include <mutex>

namespace DetourNavigator
{
    class UpdateGuard
    {
    public:
        explicit UpdateGuard(std::mutex& mutex)
            : mMutex(mutex)
        {
        }

    private:
        std::mutex& mMutex;

        friend struct UnlockUpdateGuard;
    };

    struct UnlockUpdateGuard
    {
        void operator()(UpdateGuard* value) const { value->mMutex.unlock(); }
    };

    using ScopedUpdateGuard = std::unique_ptr<UpdateGuard, UnlockUpdateGuard>;
}
