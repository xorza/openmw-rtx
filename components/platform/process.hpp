#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

/// What this process does with itself that the operating systems spell differently: what it says
/// to its environment, which process it is, where it was started from, and the commands it runs. One header over
/// `processposix.cpp` and `processwin32.cpp`, the way `file.hpp` sits over its two, and no
/// `#ifdef` anywhere the ray tracer asks these questions.
namespace Platform::Process
{
    /// Gives `name` the value `value` in this process's environment unless the shell already gave
    /// it one: a default the program states, and never a word over the shell's. Answers whether
    /// the default took, so a program whose promise rests on it can say when the shell's word
    /// stood instead.
    bool setEnvironmentDefault(const char* name, const char* value);

    /// Gives `name` the value `value` in this process's environment, over whatever it had.
    void setEnvironment(const char* name, const char* value);

    /// Gives `name` the path `value`, over whatever it had: in the system's own spelling of a path,
    /// which on Windows is wide. A path narrowed to a `char` value there goes through the code page,
    /// and a folder named outside it is either refused or another folder.
    void setEnvironmentPath(const char* name, const std::filesystem::path& value);

    /// The path `name` holds in this process's environment, read as `setEnvironmentPath` writes
    /// one, or nothing where it holds none.
    std::optional<std::filesystem::path> environmentPath(const char* name);

    /// The file this process runs: the running file itself, not `argv[0]`, which a shell may have
    /// given as a bare name or a relative path. Nothing where the system would not say.
    std::optional<std::filesystem::path> executable();

    /// This process's command line as UTF-8, from the `argc` and `argv` its `main` was handed: on
    /// Windows from the wide line the system keeps, because `argv` is in the system's code page
    /// there.
    std::vector<std::string> commandLine(int argc, char** argv);

    /// This process's id, as the system numbers processes: what a reading that names processes
    /// tells this one from the rest by.
    std::uint32_t currentId();

    /// Whether a process with the id `id` is running now, whoever owns it. An id the system never
    /// hands out answers no, nought and one past `pid_t` among them, which POSIX reads as a group
    /// or as every process. An id is used again once its process ends, and on POSIX a process that
    /// ended is still there until its parent collects it, so a yes can be another process's or one
    /// that is over: what this settles exactly is the no.
    bool isRunning(std::uint32_t id);

    /// The system's id of the calling thread: what a crash dump and a debugger number threads by.
    /// Safe inside a signal handler.
    std::uint64_t currentThreadId();

    /// Whether the standard input is a terminal: a person at a shell started this process, and a
    /// dialog box is the wrong way to tell them something. Never on Windows, where the game shows its
    /// boxes however it was started, as upstream's fatal error does.
    bool startedFromTerminal();

    /// `text` as one word of the system's shell: in single quotes for a POSIX one, where nothing
    /// inside them is expanded, a quote inside closed, escaped and opened again; in double quotes
    /// for `cmd`, which has no others.
    std::string shellWord(std::string_view text);

    /// How a command the shell ran ended.
    struct CommandEnd
    {
        /// What it exited with: a status on POSIX, and on Windows the whole code, which an
        /// exception's is. Nought where a signal ended it.
        std::uint32_t mExitCode = 0;

        /// The signal that ended it, on a system that has them. Nought where it exited.
        int mSignal = 0;

        bool succeeded() const { return mExitCode == 0 && mSignal == 0; }

        std::string describe() const
        {
            return mSignal != 0 ? "signal " + std::to_string(mSignal) : "exit code " + std::to_string(mExitCode);
        }
    };

    /// Runs `line` through the system's shell and waits for it. `cmd /c` takes a line in one more
    /// pair of quotes, which this adds, so a line of `shellWord`s means the same on either.
    CommandEnd runShell(const std::string& line);

    /// Keeps every thread of this process, those running now and those started after, on the
    /// performance cores where the system has two core types, and answers how many logical CPUs that
    /// is: nought where it has one type, or does not say which is which, and the system's own choice
    /// stands. **For a measured run**: an efficiency core runs the walk at about half the speed,
    /// and which core the system picks changes from one run to the next.
    std::size_t keepToPerformanceCores();

    /// Starts this process again, from the top, with glibc's `malloc` on transparent huge pages —
    /// `GLIBC_TUNABLES=glibc.malloc.hugetlb=1` — where the shell named no tunables of its own:
    /// Linux alone, and before any thread starts, since what replaces the process keeps none.
    /// Returns where it does not restart: on another system, where the shell's word stands, and
    /// where the system would not start it again.
    ///
    /// **For a measured run.** On 4 KiB pages the heap's physical placement, which decides the
    /// cache sets it shares, is the system's draw on every run, and six runs of one build at
    /// `one-cell-walk` read walk medians of 0.40 to 0.49 ms, the frame thread's cache misses 4.5
    /// to 8.8 a thousand instructions. On huge pages the six read 0.41 each. Without address
    /// randomization they spread as far, and with one `malloc` arena further.
    void restartOnHugePages(char** argv);

    /// Whether `malloc` is on huge pages, from glibc's tunables `tunables` and the kernel's
    /// transparent huge page mode as `/sys/kernel/mm/transparent_hugepage/enabled` writes it,
    /// the one in brackets chosen: `glibc.malloc.hugetlb` at one, which asks the kernel to back the
    /// heap by `madvise` and takes where the mode is `always` or `madvise`; or at two, which takes
    /// reserved huge pages. The last setting of the name is the one glibc keeps.
    inline bool mallocOnHugePages(std::string_view tunables, std::string_view transparentMode)
    {
        constexpr std::string_view name = "glibc.malloc.hugetlb=";
        std::string_view asked;
        while (!tunables.empty())
        {
            const std::size_t end = tunables.find(':');
            const std::string_view tunable = tunables.substr(0, end);
            if (tunable.starts_with(name))
                asked = tunable.substr(name.size());
            tunables = end == std::string_view::npos ? std::string_view() : tunables.substr(end + 1);
        }

        const bool granted = transparentMode.find("[always]") != std::string_view::npos
            || transparentMode.find("[madvise]") != std::string_view::npos;
        return asked == "2" || (asked == "1" && granted);
    }

    /// `mallocOnHugePages` of this process: its environment and the kernel's mode. Never on a
    /// system that is not Linux.
    bool mallocOnHugePages();

    /// The CPUs a Linux CPU list names — `0-7,16-19`, as sysfs writes it with its line break — in
    /// the list's order, or nothing where the text is not one. A number past the kernel's own limit
    /// of 8192 CPUs is not one either, so a range cannot ask for billions of entries.
    inline std::optional<std::vector<std::uint32_t>> parseCpuList(std::string_view text)
    {
        constexpr std::uint32_t limit = 8192;
        const auto number = [](std::string_view digits, std::uint32_t& into) {
            const char* const end = digits.data() + digits.size();
            const std::from_chars_result read = std::from_chars(digits.data(), end, into);
            return !digits.empty() && read.ec == std::errc{} && read.ptr == end && into < limit;
        };

        while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
            text.remove_suffix(1);

        std::vector<std::uint32_t> cpus;
        while (true)
        {
            const std::size_t comma = text.find(',');
            const std::string_view range = text.substr(0, comma);
            const std::size_t dash = range.find('-');
            std::uint32_t first = 0;
            std::uint32_t last = 0;
            if (!number(range.substr(0, dash), first))
                return std::nullopt;
            if (dash == std::string_view::npos)
                last = first;
            else if (!number(range.substr(dash + 1), last) || last < first)
                return std::nullopt;

            for (std::uint32_t cpu = first; cpu <= last; ++cpu)
                cpus.push_back(cpu);
            if (comma == std::string_view::npos)
                return cpus;
            text.remove_prefix(comma + 1);
        }
    }

    /// Leaves the system nothing to keep of this process when it aborts: for a process that dies on
    /// purpose, as a death test's child does, whose core nobody wants.
    void disableCoreDump();
}
