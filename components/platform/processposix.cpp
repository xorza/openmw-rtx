#include "process.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

// The system calls that are each system's own: a thread's id, and Linux's way to leave no core.
#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <pthread.h>
#elif defined(__FreeBSD__)
#include <pthread_np.h>
#else
#include <functional>
#include <thread>
#endif

namespace Platform::Process
{
    bool setEnvironmentDefault(const char* name, const char* value)
    {
        if (std::getenv(name) != nullptr)
            return false;

        setenv(name, value, 0);
        return true;
    }

    void setEnvironment(const char* name, const char* value)
    {
        setenv(name, value, 1);
    }

    std::uint32_t currentId()
    {
        return static_cast<std::uint32_t>(getpid());
    }

    bool isRunning(const std::uint32_t id)
    {
        if (id == 0 || id > static_cast<std::uint32_t>(std::numeric_limits<pid_t>::max()))
            return false;

        // Signal nought checks and sends nothing; a process of another user answers "not permitted",
        // which is an answer about a process that exists.
        return kill(static_cast<pid_t>(id), 0) == 0 || errno == EPERM;
    }

    std::uint64_t currentThreadId()
    {
#if defined(__linux__)
        return static_cast<std::uint64_t>(syscall(SYS_gettid));
#elif defined(__APPLE__)
        std::uint64_t thread = 0;
        pthread_threadid_np(nullptr, &thread);
        return thread;
#elif defined(__FreeBSD__)
        return static_cast<std::uint64_t>(pthread_getthreadid_np());
#else
        return std::hash<std::thread::id>{}(std::this_thread::get_id()) | 1;
#endif
    }

    bool startedFromTerminal()
    {
        return isatty(STDIN_FILENO) != 0;
    }

    std::string shellWord(std::string_view text)
    {
        std::string word = "'";
        for (const char c : text)
            word += c == '\'' ? std::string("'\\''") : std::string(1, c);
        return word + "'";
    }

    CommandEnd runShell(const std::string& line)
    {
        const int status = std::system(line.c_str());

        // No shell at all: what a shell answers for a command it could not run.
        if (status == -1)
            return CommandEnd{ .mExitCode = 127 };
        if (WIFSIGNALED(status))
            return CommandEnd{ .mSignal = WTERMSIG(status) };
        return CommandEnd{ .mExitCode = static_cast<std::uint32_t>(WEXITSTATUS(status)) };
    }

    void disableCoreDump()
    {
        // **Non-dumpable on Linux, where a zero core limit is not enough.** With `core_pattern` a
        // pipe to `systemd-coredump`, the kernel starts the collector for every abort whatever the
        // limit says, and the collector's start was what a death test cost: five took 964 ms, 252 ms
        // under a zero limit and 9 ms non-dumpable, which starts nothing and puts nothing in the
        // journal.
#if defined(__linux__)
        prctl(PR_SET_DUMPABLE, 0);
#else
        const rlimit none{ .rlim_cur = 0, .rlim_max = 0 };
        setrlimit(RLIMIT_CORE, &none);
#endif
    }
}
