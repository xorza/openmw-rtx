#include "crashtestssystem.hpp"

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

#include <spawn.h>
#include <sys/wait.h>
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
            return mode == "kept-abort" || mode == "kept-end" || mode == "kept-leaves"
                || mode == "kept-ignoring-children";
        }

        /// The line `kept-leaves` names the process it left with, before its id.
        constexpr std::string_view sLeft = "crash-tests left ";
    }

    Raised raisedOnThisSystem()
    {
        return Raised{
            .mFault = { "SIGSEGV at 0x10" },
            .mOverflow = { "SIGSEGV" },
            .mIllegal = { "SIGILL", "SIGTRAP" },
        };
    }

    void addModesOfThisSystem(std::vector<Mode>& into, std::string_view crashed)
    {
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
        // Started with `SIGCHLD` ignored, as a launcher may start it: the keeper still hears its
        // application end, and ends as it did.
        into.push_back({ .mName = "kept-ignoring-children",
            .mHeadline = "",
            .mRaised = {},
            .mFollows = "crash-tests lived on",
            .mReports = false,
            .mKept = true });
        // Under an image with the catcher turned off: nothing is kept, because no monitor starts.
        into.push_back({ "unkept-disabled", "", {}, "crash-tests lived on", false });
        into.push_back({ .mName = "kept-leaves",
            .mHeadline = "",
            .mRaised = {},
            .mFollows = "crash-tests lived on",
            .mReports = false,
            .mKept = true });
        // **A fork that ends through `exit`**, which keeps the reporter's exit handler and not its thread:
        // the handler has no report to wait out, and the child ends at once.
        into.push_back({ "forked-child-exits", "", {}, "crash-tests lived on", false });
        // **A report folder that takes no report**, whose catcher refuses to start and says why:
        // one that Crashpad cannot open, and one that opens and takes no new report.
        into.push_back({ .mName = "folder-unopened",
            .mHeadline = "",
            .mRaised = {},
            .mFollows = "crash-tests lived on",
            .mReports = false,
            .mRefusal = "its report folder could not be opened" });
        into.push_back({ .mName = "report-unwritable",
            .mHeadline = "",
            .mRaised = {},
            .mFollows = "crash-tests lived on",
            .mReports = false,
            .mRefusal = "its report folder takes no new report" });
        // **Stopped past the limit by somebody else**, as a shell's Ctrl+Z or a debugger stops it:
        // no frame for twice the limit, and no hang, because the game was not what stood still.
        into.push_back({ "stopped-past-limit", "", {}, "crash-tests lived on", false });
        // **Ended while its report is written**: End answered at once, so the monitor's kill would
        // land while Crashpad still reads the game, and the hang's dump has to be there all the same.
        // POSIX alone, where the end is a signal the run says.
        into.push_back({ .mName = "ended-in-report",
            .mHeadline = "Hang: no frame for",
            .mRaised = {},
            .mFollows = "crash-tests stood still",
            .mMarked = ", which draws",
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
        if (mode == "kept-ignoring-children")
            signal(SIGCHLD, SIG_IGN);
        // Read-only where Crashpad writes: the database's own folder, which it cannot then make its
        // folders in, or the folder a new report is written to.
        if (mode == "folder-unopened" || mode == "report-unwritable")
        {
            const std::filesystem::path reports = std::getenv("OPENMW_CRASH_REPORTS");
            const std::filesystem::path locked = mode == "folder-unopened" ? reports : reports / "new";
            std::filesystem::create_directories(locked);
            using std::filesystem::perms;
            std::filesystem::permissions(locked, perms::owner_write | perms::group_write | perms::others_write,
                std::filesystem::perm_options::remove);
        }
        if (mode == "unkept-disabled")
        {
            Platform::Process::setEnvironment("APPIMAGE", "crash-tests: no image, and no catcher to keep one for");
            Platform::Process::setEnvironment("OPENMW_DISABLE_CRASH_CATCHER", "1");
        }
    }

    std::optional<int> runModeOfThisSystem(std::string_view mode)
    {
        if (mode == "folder-unopened" || mode == "report-unwritable")
            return livedOn();
        if (mode == "unkept-disabled")
        {
            if (kept())
            {
                Log(Debug::Error) << "crash-tests is kept with no catcher to keep it for";
                return 3;
            }
            return livedOn();
        }
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
            if (mode == "kept-ignoring-children")
                return livedOn();
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
        if (mode == "forked-child-exits")
        {
            const auto forked = std::chrono::steady_clock::now();
            const pid_t child = fork();
            if (child < 0)
                return 3;
            if (child == 0)
                std::exit(0);

            int ended = 0;
            if (waitpid(child, &ended, 0) != child || !WIFEXITED(ended) || WEXITSTATUS(ended) != 0)
                return 3;
            // Past the limit a real report gets, which is what a handler waiting on no thread waits.
            if (std::chrono::steady_clock::now() - forked >= Crash::sReportPatience)
                return 3;
            return livedOn();
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

    std::string_view nullDevice()
    {
        return "/dev/null";
    }

    void illegalInstruction()
    {
        __builtin_trap();
    }
}
