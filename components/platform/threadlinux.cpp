#include "thread.hpp"

#include <chrono>
#include <ctime>
#include <string>
#include <string_view>
#include <system_error>

#include <pthread.h>
#include <sched.h>

namespace Platform
{
    void nameThisThread(const std::string_view name)
    {
        // Linux refuses a name of sixteen bytes or more, terminator included, rather than cutting it.
        const std::string kept(name.substr(0, sThreadNameLength));
        pthread_setname_np(pthread_self(), kept.c_str());
    }

    std::string nameOfThisThread()
    {
        char name[64] = {};
        pthread_getname_np(pthread_self(), name, sizeof(name));
        return name;
    }

    std::chrono::nanoseconds cpuTimeOfThisThread()
    {
        timespec spent{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &spent);
        return std::chrono::seconds(spent.tv_sec) + std::chrono::nanoseconds(spent.tv_nsec);
    }

    std::error_code lowerThisThreadToIdle()
    {
        sched_param param{};
        param.sched_priority = 0;
        return std::error_code(pthread_setschedparam(pthread_self(), SCHED_IDLE, &param), std::generic_category());
    }
}
