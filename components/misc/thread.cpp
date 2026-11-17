#include "thread.hpp"

#include <system_error>
#include <thread>

#include <components/debug/debuglog.hpp>
#include <components/platform/thread.hpp>

namespace Misc
{
    void setCurrentThreadIdlePriority()
    {
        if (const std::error_code refused = Platform::lowerThisThreadToIdle(); !refused)
            Log(Debug::Verbose) << "Using idle priority for thread=" << std::this_thread::get_id();
        else
            Log(Debug::Warning) << "Failed to set idle priority for thread=" << std::this_thread::get_id() << ": "
                                << refused.message();
    }
}
