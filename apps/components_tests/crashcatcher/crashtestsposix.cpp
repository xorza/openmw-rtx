#include "crashtestssystem.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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
#include <components/crashcatcher/crashinstall.hpp>
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
        // **Ended by its monitor**: End answered at once, after the hang's dump, which has to be there.
        // POSIX alone, where the end is a signal the run says.
        into.push_back({ .mName = "ended-at-once",
            .mHeadline = "Hang: no frame for",
            .mRaised = {},
            .mFollows = "crash-tests stood still",
            .mMarked = ", which draws",
            .mEndsBy = SIGKILL });
        // **A crash whose dump could not be written**: the database's folder for new reports refused
        // it. The session is packaged all the same, with the log alone, which says so. It ends by any
        // failure: a sanitizer reports a crash its handler passed on as one of its own.
        into.push_back({ .mName = "dump-unwritten",
            .mHeadline = "Crash: ",
            .mRaised = {},
            .mFollows = "Crash: the game crashed, and no dump of it could be written",
            .mReports = false,
            .mLogAlone = true });
        // **A monitor gone before the crash**: the crash ends the game at once and says it goes
        // unreported, where Crashpad would hand it to no one; by any failure, as the one above.
        into.push_back({ .mName = "monitor-killed",
            .mHeadline = "Crash: ",
            .mRaised = {},
            .mFollows = "Crash: the crash catcher's monitor has ended, and this crash goes unreported: crash-tests "
                        "gave up alone",
            .mReports = false });
        // **A handler another installs after the catcher is said**: the renderer's start logs each
        // hook `Crash::lostHooks` names, and one that names none, or another, fails the mode.
        into.push_back({ "hook-lost", "", {}, "crash-tests lost SIGSEGV alone", false });
        // **Ended from outside**, past every handler, as an out-of-memory kill ends it: only the
        // missing end word says so.
        into.push_back({ .mName = "killed-from-outside",
            .mHeadline = "",
            .mRaised = {},
            .mFollows = "The game ended without reaching its end and left no report",
            .mReports = false,
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
        if (mode == "dump-unwritten")
        {
            // After the monitor probed it, which it must pass to watch at all.
            const std::filesystem::path fresh = std::filesystem::path(std::getenv("OPENMW_CRASH_REPORTS")) / "new";
            using std::filesystem::perms;
            std::filesystem::permissions(fresh, perms::owner_write | perms::group_write | perms::others_write,
                std::filesystem::perm_options::remove);
            raise(SIGSEGV);
            return 3;
        }
        if (mode == "monitor-killed")
        {
            // The monitor is the one process the log names as it; gone once its state is dead or a
            // zombie, which a container with no reaper leaves it, **and its last thread has ended**:
            // a killed process's first thread turns zombie before the others end, and its files,
            // the monitor's end of the connection among them, close with the last.
            const std::string log
                = contentsOf(std::filesystem::path(std::getenv("OPENMW_CRASH_REPORTS")).parent_path().parent_path()
                    / "crash-tests.log");
            constexpr std::string_view named = "from the monitor process ";
            const std::size_t at = log.find(named);
            if (at == std::string::npos)
                return 3;
            const pid_t monitor = std::atoi(log.c_str() + at + named.size());
            if (monitor <= 0 || kill(monitor, SIGKILL) != 0)
                return 3;
            const auto running = [&] {
                const std::string stat = contentsOf("/proc/" + std::to_string(monitor) + "/stat");
                const std::size_t state = stat.rfind(')') + 2;
                if (state > 1 && state < stat.size() && stat[state] != 'Z' && stat[state] != 'X')
                    return true;
                std::error_code unread;
                const auto threads = std::distance(
                    std::filesystem::directory_iterator("/proc/" + std::to_string(monitor) + "/task", unread),
                    std::filesystem::directory_iterator());
                return !unread && threads > 1;
            };
            for (int waited = 0; running(); ++waited)
            {
                if (waited == 1000)
                    return 3;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Crash::fatal("crash-tests gave up alone");
        }
        if (mode == "hook-lost")
        {
            if (!Crash::lostHooks().empty())
                return 3;
            struct sigaction ignored{};
            ignored.sa_handler = SIG_IGN;
            sigemptyset(&ignored.sa_mask);
            struct sigaction kept{};
            sigaction(SIGSEGV, &ignored, &kept);
            const std::vector<std::string_view> lost = Crash::lostHooks();
            sigaction(SIGSEGV, &kept, nullptr);
            if (lost == std::vector<std::string_view>{ "SIGSEGV" } && Crash::lostHooks().empty())
                Log(Debug::Info) << "crash-tests lost SIGSEGV alone";
            return livedOn();
        }
        if (mode == "killed-from-outside")
        {
            for (int i = 0; i < 5; ++i)
            {
                Crash::heartbeat();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            raise(SIGKILL);
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
                // By the name it was started as, `comm`, and not its executable: where coreutils is one program
                // for every command, AlmaLinux's container among them, `sleep`'s executable is `coreutils`.
                std::ifstream comm("/proc/" + std::to_string(left) + "/comm");
                std::string name;
                std::getline(comm, name);
                if (name != "sleep")
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
