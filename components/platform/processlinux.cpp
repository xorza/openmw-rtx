#include "process.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <sched.h>
// NOLINTNEXTLINE(modernize-deprecated-headers): POSIX's `kill`, which <csignal> need not declare
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "folder.hpp"
#include "kernelfilelinux.hpp"
#include "linuxtext.hpp"

namespace Platform::Process
{
    bool setEnvironmentDefault(const char* name, const char* value)
    {
        if (std::getenv(name) != nullptr)
            return false;

        setenv(name, value, 0); // NOLINT(concurrency-mt-unsafe): before any thread starts, as the header says
        return true;
    }

    void setEnvironment(const char* name, const char* value)
    {
        setenv(name, value, 1); // NOLINT(concurrency-mt-unsafe): before any thread starts, as the header says
    }

    void setEnvironmentPath(const char* name, const std::filesystem::path& value)
    {
        setenv(name, value.c_str(), 1); // NOLINT(concurrency-mt-unsafe): before any thread starts, as the header says
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
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
        if (error)
            return std::nullopt;
        return path;
    }

    std::vector<std::string> commandLine(const std::int32_t argc, char** const argv)
    {
        return std::vector<std::string>(argv, argv + argc);
    }

    void attachParentConsole() {}

    bool standardErrorTakesColour()
    {
        return std::getenv("TERM") != nullptr && isatty(STDERR_FILENO) != 0;
    }

    std::string_view executableSuffix()
    {
        return "";
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
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): Linux's syscall
        return static_cast<std::uint64_t>(syscall(SYS_gettid));
    }

    void restartOnHugePages(char** argv)
    {
        if (!setEnvironmentDefault("GLIBC_TUNABLES", "glibc.malloc.hugetlb=1"))
            return;

        // Returns only where the system would not start it, and the run goes on as it was, with
        // the environment as it was: a tunable glibc read none of is not one to hand on.
        if (const std::optional<std::filesystem::path> self = executable())
            execv(self->c_str(), argv);
        unsetenv("GLIBC_TUNABLES"); // NOLINT(concurrency-mt-unsafe): the process's first act, before any thread
    }

    bool refuseTransparentHugePages()
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): Linux's prctl
        return prctl(PR_SET_THP_DISABLE, 1, 0, 0, 0) == 0;
    }

    std::optional<float> hugePageShare()
    {
        // The rollup is some twenty short lines.
        std::array<char, 4096> buffer{};
        const std::optional<std::string_view> rollup = KernelFile::read("/proc/self/smaps_rollup", buffer);
        if (!rollup.has_value())
            return std::nullopt;
        return LinuxText::hugePageShare(*rollup);
    }

    std::optional<std::uint64_t> compactionStalls()
    {
        // The counters are some hundred and fifty short lines.
        std::array<char, 16384> buffer{};
        const std::optional<std::string_view> vmstat = KernelFile::read("/proc/vmstat", buffer);
        if (!vmstat.has_value())
            return std::nullopt;
        return LinuxText::compactionStalls(*vmstat);
    }

    std::optional<AnonymousMemory> anonymousMemory()
    {
        // The status is some sixty short lines.
        std::array<char, 8192> buffer{};
        const std::optional<std::string_view> status = KernelFile::read("/proc/self/status", buffer);
        if (!status.has_value())
            return std::nullopt;
        return LinuxText::anonymousMemory(*status);
    }

    std::optional<PrivateMemoryHeld> privateMemoryHeld()
    {
        return std::nullopt;
    }

    std::optional<DriverMemory> driverMemory()
    {
        // Every open file's information, the DRM clients among them: one for the device, and any a library opened.
        // **Each client once**: a file duplicated or shared states its client again, and the handheld's Vulkan
        // device is two files of one client, which read the process's memory twice.
        std::optional<DriverMemory> summed;
        std::vector<std::uint64_t> counted;
        std::error_code error;
        std::array<char, 8192> buffer{};
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::directory_iterator("/proc/self/fdinfo", error))
        {
            const std::optional<std::string_view> text = KernelFile::read(entry.path().c_str(), buffer);
            if (!text.has_value())
                continue;
            const std::optional<LinuxText::DriverClient> client = LinuxText::driverClient(*text);
            if (!client.has_value() || std::ranges::find(counted, client->mId) != counted.end())
                continue;
            counted.push_back(client->mId);
            if (!summed.has_value())
                summed.emplace();
            DriverMemory& into = *summed;
            const DriverMemory& held = client->mMemory;
            into.mTotalVram += held.mTotalVram;
            into.mResidentVram += held.mResidentVram;
            into.mTotalGtt += held.mTotalGtt;
            into.mResidentGtt += held.mResidentGtt;
            into.mResidentCpu += held.mResidentCpu;
        }
        return summed;
    }

    std::size_t keepToPerformanceCores()
    {
        // The kernel lists each core type apart only on a hybrid part, which is the one case there is
        // a choice to make. Sysfs writes less than a page, and 64 KiB is the largest page x86-64 and
        // arm64 have.
        std::array<char, 65536> buffer{};
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
    }

    std::size_t usableCpuCount()
    {
        // A mask past `CPU_SETSIZE`, a machine of more than 1024 CPUs, is refused, and every CPU counts.
        cpu_set_t set;
        CPU_ZERO(&set);
        if (sched_getaffinity(0, sizeof(set), &set) == 0)
            return std::max<std::size_t>(1, static_cast<std::size_t>(CPU_COUNT(&set)));
        return std::max<std::size_t>(1, std::thread::hardware_concurrency());
    }

    bool startedFromTerminal()
    {
        return isatty(STDIN_FILENO) != 0;
    }

    bool showsADesktop()
    {
        return true;
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
        // Runs the command line its caller built.
        // NOLINTNEXTLINE(bugprone-command-processor)
        const std::int32_t status = std::system(line.c_str());

        // No shell at all: what a shell answers for a command it could not run.
        if (status == -1)
            return CommandEnd{ .mExitCode = 127 };
        if (WIFSIGNALED(status))
            return CommandEnd{ .mSignal = WTERMSIG(status) };
        return CommandEnd{ .mExitCode = static_cast<std::uint32_t>(WEXITSTATUS(status)) };
    }
}
