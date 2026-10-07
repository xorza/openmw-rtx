#include "crashtestssystem.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <pthread.h>
#include <spawn.h>
#include <unistd.h>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>
#include <components/platform/process.hpp>

namespace CrashTests
{
    namespace
    {
        /// Whether this process is the application a keeper forked: its parent runs the command line
        /// a fork copies, and the variable the keeper sets stands.
        bool kept()
        {
            return std::getenv("OPENMW_IMAGE_KEPT") != nullptr
                && contentsOf("/proc/" + std::to_string(getppid()) + "/cmdline") == contentsOf("/proc/self/cmdline");
        }

        bool keptMode(std::string_view mode)
        {
            return mode == "kept-abort" || mode == "kept-end" || mode == "kept-leaves";
        }

        /// The line `kept-leaves` names the process it left with, before its id.
        constexpr std::string_view sLeft = "crash-tests left ";
    }

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
        // Under an AppImage's keeper: a crash, whose package the run outlasts; a termination the
        // keeper is sent and passes on; and a process from outside the image, which neither the
        // keeper waits for nor the monitor, though it inherits what the application lets it.
        into.push_back({ .mName = "kept-abort",
            .mHeadline = "Crash: ",
            .mRaised = { "SIGABRT" },
            .mFollows = "crash-tests kept",
            .mMarked = crashed,
            .mEndsBy = SIGABRT,
            .mKept = true });
        into.push_back({ .mName = "kept-end",
            .mHeadline = "",
            .mRaised = {},
            .mFollows = "crash-tests kept",
            .mReports = false,
            .mEndsBy = SIGTERM,
            .mKept = true });
        into.push_back({ .mName = "kept-leaves",
            .mHeadline = "",
            .mRaised = {},
            .mFollows = "crash-tests lived on",
            .mReports = false,
            .mKept = true });
#endif
        into.push_back({ "report-under-hang", "Report: crash-tests asked under a hang request", {},
            "crash-tests lived on", true, ", which asked" });
        // **Ended while its report is written**: End answered at once, so the monitor's kill would
        // land while Crashpad still reads the game, and the hang's dump has to be there all the same.
        // POSIX alone, where the end is a signal the run says.
        // **Stopped past the limit by somebody else**, as a shell's Ctrl+Z or a debugger stops it:
        // no frame for twice the limit, and no hang, because the game was not what stood still.
        into.push_back({ "stopped-past-limit", "", {}, "crash-tests lived on", false });
        into.push_back({ .mName = "ended-in-report",
            .mHeadline = "Hang: no frame for",
            .mRaised = {},
            .mFollows = "crash-tests stood still",
            .mEndsBy = SIGKILL });
    }

    void prepareModeOfThisSystem(std::string_view mode)
    {
        // The keeper asks whether `APPIMAGE` stands, and nothing of where it points; the image is
        // this binary's folder, so the monitor runs a program from it and `sleep` does not.
        if (keptMode(mode))
        {
            Platform::Process::setEnvironment("APPIMAGE", "crash-tests: no image, kept all the same");
            Platform::Process::setEnvironmentPath(
                "APPDIR", std::filesystem::read_symlink("/proc/self/exe").parent_path());
        }
    }

    std::optional<int> runModeOfThisSystem(std::string_view mode)
    {
        if (keptMode(mode))
        {
            if (!kept())
            {
                Log(Debug::Error) << "crash-tests is not the application of a keeper";
                return 3;
            }
            Log(Debug::Info) << "crash-tests kept";
            if (mode == "kept-abort")
                std::abort();
            if (mode == "kept-leaves")
            {
                // Long enough that a keeper waiting for it outlasts the matrix's check of it; and with
                // every descriptor the application lets it inherit, as a program the game starts has
                // them: one holding the catcher's socket would keep the monitor, which is the image's.
                char sleep[] = "sleep";
                char seconds[] = "60";
                char* arguments[] = { sleep, seconds, nullptr };
                pid_t left = 0;
                if (posix_spawn(&left, "/bin/sleep", nullptr, nullptr, arguments, environ) != 0)
                    return 3;
                Log(Debug::Info) << sLeft << left;
                return livedOn();
            }
            kill(getppid(), SIGTERM);
            for (;;)
                pause();
        }
        if (mode == "stopped-past-limit")
        {
            for (int i = 0; i < 5; ++i)
            {
                Crash::heartbeat();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // Somebody else's continue, four seconds on: twice the limit.
            const std::string resume = "sleep 4; kill -CONT " + std::to_string(getpid());
            char shell[] = "sh";
            char command[] = "-c";
            std::string line = resume;
            char* arguments[] = { shell, command, line.data(), nullptr };
            pid_t continuer = 0;
            if (posix_spawn(&continuer, "/bin/sh", nullptr, nullptr, arguments, environ) != 0)
                return 3;
            raise(SIGSTOP);

            for (int i = 0; i < 20; ++i)
            {
                Crash::heartbeat();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            return livedOn();
        }
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

    std::optional<std::string> checkModeOfThisSystem(std::string_view mode, const std::filesystem::path& folder)
    {
        if (mode != "kept-leaves")
            return std::nullopt;

        std::ifstream log(folder / "crash-tests.log");
        for (std::string line; std::getline(log, line);)
            if (const std::size_t at = line.find(sLeft); at != std::string::npos)
            {
                const pid_t left = std::stoi(line.substr(at + sLeft.size()));
                std::error_code gone;
                const std::filesystem::path program
                    = std::filesystem::read_symlink("/proc/" + std::to_string(left) + "/exe", gone);
                if (gone || program.filename() != "sleep")
                    return "the run lasted as long as the process it left, which neither the keeper nor the monitor "
                           "may wait for";
                kill(left, SIGKILL);
                return std::nullopt;
            }
        return "the mode named no process it left";
    }

    void illegalInstruction()
    {
        __builtin_trap();
    }
}
