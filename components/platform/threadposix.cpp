#include "thread.hpp"

#include <chrono>
#include <string>
#include <string_view>

#include <pthread.h>
#include <time.h>

// Each system's call for a thread's name: Linux's and FreeBSD's name any thread and Apple's only
// the calling one.
#if defined(__FreeBSD__)
#include <pthread_np.h>
#endif

namespace Platform
{
    void nameThisThread(const std::string_view name)
    {
        // Linux refuses a name of sixteen bytes or more, terminator included, rather than cutting it.
        const std::string kept(name.substr(0, sThreadNameLength));
#if defined(__APPLE__)
        pthread_setname_np(kept.c_str());
#else
        pthread_setname_np(pthread_self(), kept.c_str());
#endif
    }

    std::string nameOfThisThread()
    {
        char name[64] = {};
#if defined(__FreeBSD__)
        pthread_get_name_np(pthread_self(), name, sizeof(name));
#else
        pthread_getname_np(pthread_self(), name, sizeof(name));
#endif
        return name;
    }

    std::chrono::nanoseconds cpuTimeOfThisThread()
    {
        timespec spent{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &spent);
        return std::chrono::seconds(spent.tv_sec) + std::chrono::nanoseconds(spent.tv_nsec);
    }
}
