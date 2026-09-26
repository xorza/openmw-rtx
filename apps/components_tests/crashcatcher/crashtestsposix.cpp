#include "crashtestssystem.hpp"

#include <csignal>
#include <thread>

#include <pthread.h>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>

namespace CrashTests
{
    Raised raisedOnThisSystem()
    {
#if defined(__APPLE__)
        return Raised{
            .mFault = { "EXC_BAD_ACCESS at 0x10" },
            .mOverflow = { "EXC_BAD_ACCESS" },
            .mIllegal = { "EXC_BAD_INSTRUCTION", "EXC_BREAKPOINT" },
        };
#else
        return Raised{
            .mFault = { "SIGSEGV at 0x10" },
            .mOverflow = { "SIGSEGV" },
            .mIllegal = { "SIGILL", "SIGTRAP" },
        };
#endif
    }

    void addModesOfThisSystem(std::vector<Mode>& into, std::string_view crashed)
    {
#if defined(__APPLE__)
        into.push_back({ "abort", "Crash: ", { "EXC_CRASH", "SIGABRT" }, {}, true, crashed });
#else
        into.push_back({ "abort", "Crash: ", { "SIGABRT" }, {}, true, crashed });
#endif
        into.push_back({ "report-under-hang", "Report: crash-tests asked under a hang request", {},
            "crash-tests lived on", true, ", which asked" });
    }

    std::optional<int> runModeOfThisSystem(std::string_view mode)
    {
        if (mode == "report-under-hang")
        {
            // **One hang request, at the reporting thread, once its report is being written**: the
            // request lands between the report saying what it is and its dump, which is where one
            // wrote over both. The dump takes the monitor tens of milliseconds, so the request lands
            // inside it.
            const pthread_t reporter = pthread_self();
            std::thread asker([reporter] {
                while (!Crash::isReporting())
                    std::this_thread::yield();
                pthread_kill(reporter, SIGUSR2);
            });
            Crash::report("crash-tests asked under a hang request");
            asker.join();
            return livedOn();
        }
        return std::nullopt;
    }

    void illegalInstruction()
    {
        __builtin_trap();
    }
}
