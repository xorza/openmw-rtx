#include "crashtestssystem.hpp"

#include <atomic>
#include <csignal>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include <pthread.h>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>

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
            // wrote over both.
            //
            // **The report starts once the asker is waiting, and the asker stops once the report
            // ended.** A report can be over in a few milliseconds, and a busy machine — the matrix
            // runs every mode at once, beside the other suites — can keep the asker off the
            // processor for all of them; an asker that waited for nothing else then waited forever.
            // A run whose request did not land inside the report says so, and the matrix runs it
            // again.
            const pthread_t reporter = pthread_self();
            std::atomic<bool> waiting{ false };
            std::atomic<bool> ended{ false };
            std::atomic<bool> landed{ false };
            std::thread asker([&] {
                waiting.store(true, std::memory_order_release);
                while (!Crash::isReporting())
                {
                    if (ended.load(std::memory_order_acquire))
                        return;
                    std::this_thread::yield();
                }
                pthread_kill(reporter, SIGUSR2);
                landed.store(Crash::isReporting(), std::memory_order_release);
            });

            while (!waiting.load(std::memory_order_acquire))
                std::this_thread::yield();
            Crash::report("crash-tests asked under a hang request");
            ended.store(true, std::memory_order_release);
            asker.join();

            if (!landed.load(std::memory_order_acquire))
            {
                Log(Debug::Warning) << "crash-tests: the hang request missed the report";
                return sInconclusive;
            }
            return livedOn();
        }
        return std::nullopt;
    }

    void illegalInstruction()
    {
        __builtin_trap();
    }
}
