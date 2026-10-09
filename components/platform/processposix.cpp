#include "process.hpp"

#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "folder.hpp"
#include "linuxtext.hpp"

// The system calls that are each system's own: a thread's id, the running file, and Linux's way to
// keep the threads to the performance cores.
#if defined(__linux__)
#include <array>
#include <set>

#include <sched.h>
#include <sys/syscall.h>

#include "kernelfile.hpp"
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
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

    void setEnvironmentPath(const char* name, const std::filesystem::path& value)
    {
        setenv(name, value.c_str(), 1);
    }

    std::optional<std::filesystem::path> environmentPath(const char* name)
    {
        const char* const value = std::getenv(name);
        if (value == nullptr)
            return std::nullopt;
        return std::filesystem::path(value);
    }

    std::optional<std::filesystem::path> executable()
    {
        std::error_code error;
#if defined(__APPLE__)
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string path(size, '\0');
        if (_NSGetExecutablePath(path.data(), &size) != 0)
            return std::nullopt;

        std::filesystem::path resolved = std::filesystem::canonical(path.c_str(), error);
        if (error)
            return std::nullopt;
        return resolved;
#else
        // Linux names it the first way, and the BSDs' process file systems one of the others.
        for (const char* link : { "/proc/self/exe", "/proc/self/file", "/proc/curproc/exe", "/proc/curproc/file" })
        {
            std::filesystem::path path = std::filesystem::read_symlink(link, error);
            if (!error)
                return path;
        }
        return std::nullopt;
#endif
    }

    std::vector<std::string> commandLine(const int argc, char** const argv)
    {
        return std::vector<std::string>(argv, argv + argc);
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

    void restartOnHugePages([[maybe_unused]] char** argv)
    {
#if defined(__linux__)
        if (!setEnvironmentDefault("GLIBC_TUNABLES", "glibc.malloc.hugetlb=1"))
            return;

        // Returns only where the system would not start it, and the run goes on as it was, with
        // the environment as it was: a tunable glibc read none of is not one to hand on.
        if (const std::optional<std::filesystem::path> self = executable())
            execv(self->c_str(), argv);
        unsetenv("GLIBC_TUNABLES");
#endif
    }

    std::optional<float> hugePageShare()
    {
#if defined(__linux__)
        // The rollup is some twenty short lines.
        std::array<char, 4096> buffer;
        const std::optional<std::string_view> rollup = KernelFile::read("/proc/self/smaps_rollup", buffer);
        if (!rollup.has_value())
            return std::nullopt;
        return LinuxText::hugePageShare(*rollup);
#else
        return std::nullopt;
#endif
    }

    std::size_t keepToPerformanceCores()
    {
#if defined(__linux__)
        // The kernel lists each core type apart only on a hybrid part, which is the one case there is
        // a choice to make. Sysfs writes less than a page, and 64 KiB is the largest page x86-64 and
        // arm64 have.
        std::array<char, 65536> buffer;
        const std::optional<std::string_view> text = KernelFile::read("/sys/devices/cpu_core/cpus", buffer);
        if (!text.has_value())
            return 0;
        const std::optional<std::vector<std::uint32_t>> cpus = LinuxText::parseCpuList(*text);
        if (!cpus.has_value())
            return 0;

        cpu_set_t set;
        CPU_ZERO(&set);
        for (const std::uint32_t cpu : *cpus)
        {
            if (cpu >= CPU_SETSIZE)
                return 0;
            CPU_SET(cpu, &set);
        }

        // **Every thread, and again until a pass finds none new**: the mask is a thread's own, and a
        // thread started while the first pass ran took its maker's mask from before.
        std::set<pid_t> kept;
        for (bool found = true; found;)
        {
            found = false;
            const std::optional<std::vector<std::filesystem::directory_entry>> threads = listFolder("/proc/self/task");
            if (!threads.has_value())
                return 0;
            for (const std::filesystem::directory_entry& entry : *threads)
            {
                pid_t thread = 0;
                const std::string name = entry.path().filename().native();
                if (std::from_chars(name.data(), name.data() + name.size(), thread).ec != std::errc{}
                    || !kept.insert(thread).second)
                    continue;
                found = true;
                // A thread that ended since the listing is not an error.
                if (sched_setaffinity(thread, sizeof(set), &set) != 0 && errno != ESRCH)
                    return 0;
            }
        }

        return cpus->size();
#else
        return 0;
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
}
