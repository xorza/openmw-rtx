#include "crashinstall.hpp"

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <components/files/conversion.hpp>
#include <components/platform/process.hpp>

// **An AppImage's mount outlives every process the image starts.** The type2 runtime serves the
// image from a FUSE daemon that unmounts once no process holds the read end of its keepalive pipe,
// which reaches a process by inheritance alone; Crashpad starts a monitor with every descriptor but
// its socket closed, in a session of its own, to outlive its client. A monitor whose client was the
// last holder then paged its code in from a mount that was gone, and ended with SIGBUS. So the
// process the runtime started stays, holding the pipe, as the subreaper every orphan of the image is
// handed to, and ends once none of them runs a program from the image.
namespace Crash
{
    namespace
    {
        /// Set before the keeper forks, so neither the application nor anything it starts keeps the
        /// image a second time.
        constexpr const char* sKeptVariable = "OPENMW_IMAGE_KEPT";

        /// **Ends the keeper by `signal`, and leaves no core**: the application wrote the one that
        /// counts, and a second core per crash is what the keeper exists to stop.
        [[noreturn]] void endBy(int signal)
        {
            prctl(PR_SET_DUMPABLE, 0);
            std::signal(signal, SIG_DFL);
            sigset_t raised;
            sigemptyset(&raised);
            sigaddset(&raised, signal);
            sigprocmask(SIG_UNBLOCK, &raised, nullptr);
            raise(signal);
            _exit(128 + signal);
        }

        /// The application's end as the keeper's own, so whoever started the image reads a crash as
        /// a crash. `_exit`, because the keeper's statics are the application's, copied by the fork.
        [[noreturn]] void endAs(int status)
        {
            if (WIFEXITED(status))
                _exit(WEXITSTATUS(status));
            endBy(WTERMSIG(status));
        }

        /// A process and its parent, as `/proc/<id>/stat` gives them.
        struct Parented
        {
            pid_t mId;
            pid_t mParent;
        };

        /// **Whether a process the keeper started or adopted still runs a program from `appDir`**:
        /// the application, a monitor, the game. One that runs another, a browser a dialog opened,
        /// needs no mount, and goes to the next subreaper when the keeper ends. A process whose
        /// program cannot be read counts as the image's, so the mount stays where the keeper
        /// cannot tell.
        bool imageRuns(pid_t keeper, const std::string& appDir)
        {
            std::vector<Parented> processes;
            std::error_code unread;
            for (const auto& entry : std::filesystem::directory_iterator("/proc", unread))
            {
                const std::string name = Files::pathToUnicodeString(entry.path().filename());
                if (name.find_first_not_of("0123456789") != std::string::npos)
                    continue;
                std::ifstream file(entry.path() / "stat");
                const std::string stat((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                // After the name, which may hold any character but ends at the last parenthesis: the
                // state, then the parent.
                const std::size_t named = stat.rfind(')');
                if (named == std::string::npos || named + 4 >= stat.size())
                    continue;
                processes.push_back(Parented{ .mId = static_cast<pid_t>(std::stol(name)),
                    .mParent = static_cast<pid_t>(std::stol(stat.substr(named + 4))) });
            }

            std::vector<pid_t> descendants{ keeper };
            for (std::size_t at = 0; at < descendants.size(); ++at)
                for (const Parented& process : processes)
                    if (process.mParent == descendants[at])
                        descendants.push_back(process.mId);

            for (std::size_t at = 1; at < descendants.size(); ++at)
            {
                std::error_code unknown;
                const std::filesystem::path program
                    = std::filesystem::read_symlink("/proc/" + std::to_string(descendants[at]) + "/exe", unknown);
                // Gone since the listing, or a zombie, which has no program.
                if (unknown == std::errc::no_such_file_or_directory)
                    continue;
                if (unknown || Files::pathToUnicodeString(program).starts_with(appDir))
                    return true;
            }
            return false;
        }

        /// **On one thread, by `sigwaitinfo` and no handler**, so a termination is passed on only
        /// while the application is not yet reaped, and never to a process that took its id after.
        /// Without an `APPDIR` it can resolve, as `/proc/<id>/exe` names programs, it waits for every
        /// orphan, which cannot end the mount early.
        [[noreturn]] void keep(pid_t application, const sigset_t& waited)
        {
            std::string appDir;
            if (const char* const variable = std::getenv("APPDIR"))
            {
                std::error_code unresolved;
                const std::filesystem::path resolved = std::filesystem::canonical(variable, unresolved);
                if (!unresolved)
                    appDir = Files::pathToUnicodeString(resolved) + "/";
            }
            const pid_t keeper = getpid();
            bool applicationEnded = false;
            int applicationEnd = 0;
            for (;;)
            {
                siginfo_t info{};
                const int signal = sigwaitinfo(&waited, &info);
                if (signal == -1)
                    continue;

                if (signal != SIGCHLD)
                {
                    // One the kernel sent the process group, a terminal's among them, reached the
                    // application too. What comes once it ended is for the keeper itself.
                    if (applicationEnded)
                        endBy(signal);
                    if (info.si_code != SI_KERNEL)
                        kill(application, signal);
                    continue;
                }

                for (;;)
                {
                    int status = 0;
                    const pid_t ended = waitpid(-1, &status, WNOHANG);
                    if (ended == application)
                    {
                        applicationEnded = true;
                        applicationEnd = status;
                    }
                    else if (ended == 0)
                    {
                        if (applicationEnded && !appDir.empty() && !imageRuns(keeper, appDir))
                            endAs(applicationEnd);
                        break;
                    }
                    else if (ended == -1 && errno == ECHILD)
                        endAs(applicationEnd);
                }
            }
        }
    }

    std::string_view keepImageMounted()
    {
        if (std::getenv("APPIMAGE") == nullptr || std::getenv(sKeptVariable) != nullptr)
            return {};
        Platform::Process::setEnvironment(sKeptVariable, "1");

        // An orphan goes to the nearest subreaper among its ancestors, and a monitor is one from its
        // start: the double fork that spawns it leaves it no parent.
        if (prctl(PR_SET_CHILD_SUBREAPER, 1) != 0)
            return "the system refused a subreaper";

        // Blocked before the fork, so an end or a termination that comes before the keeper waits is
        // held for it; the application takes its own mask back.
        sigset_t waited;
        sigemptyset(&waited);
        for (const int signal : { SIGCHLD, SIGTERM, SIGINT, SIGHUP, SIGQUIT })
            sigaddset(&waited, signal);
        sigset_t previous;
        sigprocmask(SIG_BLOCK, &waited, &previous);

        // **The keeper's own disposition of `SIGCHLD`**: one inherited as ignored has the kernel reap
        // every child itself and send no `SIGCHLD`, and the keeper, waiting for one, would outlive the
        // application and hold the mount. The application takes back what it inherited.
        struct sigaction inherited
        {
        };
        struct sigaction reaped
        {
        };
        reaped.sa_handler = SIG_DFL;
        sigemptyset(&reaped.sa_mask);
        sigaction(SIGCHLD, &reaped, &inherited);

        const pid_t application = fork();
        if (application > 0)
            keep(application, waited);

        sigaction(SIGCHLD, &inherited, nullptr);
        sigprocmask(SIG_SETMASK, &previous, nullptr);
        if (application == -1)
        {
            // Or the application would adopt its own monitor, and leave it a zombie.
            prctl(PR_SET_CHILD_SUBREAPER, 0);
            return "no process could be made";
        }
        return {};
    }
}
