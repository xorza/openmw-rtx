#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// What this process does with itself that the operating systems spell differently: what it says
/// to its environment, which process it is, where it was started from, and the commands it runs. One header over
/// `processlinux.cpp` and `processwin32.cpp`, the way `file.hpp` sits over its two, and no
/// `#ifdef` anywhere the ray tracer asks these questions.
namespace Platform::Process
{
    /// The three below write the process's environment, which a read on another thread races: each is called
    /// before the process starts a thread, as a program's settings of its own libraries are.

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
    std::vector<std::string> commandLine(std::int32_t argc, char** argv);

    /// The parent's console taken on, where the process has none and its parent does, with its streams that were not
    /// redirected and UTF-8 for its output: Windows', where a program built for the window starts without one. Nothing
    /// on Linux, where a process's terminal is its parent's already.
    void attachParentConsole();

    /// Whether the standard error is a terminal that takes ANSI colours: a Windows console in virtual terminal mode,
    /// or, there and on Linux, a character device under a `TERM`.
    bool standardErrorTakesColour();

    /// What the system ends a program's file name with: `.exe` on Windows, nothing on Linux.
    std::string_view executableSuffix();

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

    /// Whether this process's session shows a desktop, which a swapchain is presented to. **A Windows
    /// session a service starts shows none**, an SSH server's among them: its window station is not visible
    /// (`WSF_VISIBLE`), though a window opens there, and an AMD driver refuses a swapchain on it with
    /// `VK_ERROR_FORMAT_NOT_SUPPORTED`. Elsewhere true: whether a display server is there is SDL's to find as
    /// it starts its video.
    bool showsADesktop();

    /// `text` as one word of the system's shell: in single quotes for a POSIX one, where nothing
    /// inside them is expanded, a quote inside closed, escaped and opened again; in double quotes
    /// for `cmd`, which has no others. Throws `std::invalid_argument` on Windows for a text that
    /// holds two `%` or a `"`: `cmd` expands a `%NAME%` inside its quotes and ends the word at a
    /// quote. A lone `%` names nothing and stays.
    std::string shellWord(std::string_view text);

    /// How a command the shell ran ended.
    struct CommandEnd
    {
        /// What it exited with: a status on POSIX, and on Windows the whole code, which an
        /// exception's is. Nought where a signal ended it.
        std::uint32_t mExitCode = 0;

        /// The signal that ended it, on a system that has them. Nought where it exited.
        std::int32_t mSignal = 0;

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

    /// How many logical CPUs this process may run its threads on: those its affinity, or on Windows its
    /// default CPU sets, leave it, so a process `keepToPerformanceCores` kept counts those it kept, and
    /// `std::thread::hardware_concurrency`, which counts every CPU the system has, where the system
    /// says nothing narrower. At least one.
    std::size_t usableCpuCount();

    /// Starts this process again, from the top, with glibc's `malloc` on transparent huge pages —
    /// `GLIBC_TUNABLES=glibc.malloc.hugetlb=1` — where the shell named no tunables of its own: Linux alone,
    /// and before any thread starts, since what replaces the process keeps none. Returns where it does not
    /// restart: on another system, where the shell's word stands, and where the system would not start it.
    ///
    /// **For a measured run.** On 4 KiB pages the heap's physical placement, which decides the cache sets it
    /// shares, is the system's draw on every run: six runs of one build at `one-cell-walk` read walk medians
    /// of 0.40 to 0.49 ms, and on huge pages 0.41 each.
    void restartOnHugePages(char** argv);

    /// Takes the process off transparent huge pages from here on, for every mapping it makes and every child it starts
    /// (Linux's `PR_SET_THP_DISABLE`), and answers whether the system took it: what a run that asks what a kernel
    /// giving huge pages to every mapping adds to the heap's free memory sets. A system with no transparent huge pages,
    /// Windows among them, already runs without, and answers true.
    bool refuseTransparentHugePages();

    /// `Platform::LinuxText::hugePageShare` of this process, now. Nothing on a system that is not Linux,
    /// or where the rollup cannot be read.
    std::optional<float> hugePageShare();

    /// `Platform::LinuxText::compactionStalls` of the machine, now: what a run on huge pages reads
    /// beside its frames, since a heap the kernel compacts for one stalls the frame that asked.
    /// Nothing on a system that is not Linux, or where the counters cannot be read.
    std::optional<std::uint64_t> compactionStalls();

    /// What a process holds of anonymous memory, as the system counts it: resident, and swapped out — on Windows the
    /// private memory committed and not in its working set, which the pagefile holds or nothing has touched yet.
    struct AnonymousMemory
    {
        std::uint64_t mResident = 0;
        std::uint64_t mSwapped = 0;
    };

    /// `AnonymousMemory` of this process, now: what a run on a machine short of memory reads at every stop, since
    /// what it holds the system takes from everything else. Nothing where the system does not say.
    std::optional<AnonymousMemory> anonymousMemory();

    /// **What holds a process's committed private memory**, split by who keeps it: the regions of the heaps the
    /// process's allocators keep, and every other private region, with how many there are and the largest of them,
    /// which is what a driver or a library's own allocation keeps.
    struct PrivateMemoryHeld
    {
        std::uint64_t mHeaps = 0;
        std::uint64_t mOther = 0;
        std::uint32_t mOtherRegions = 0;
        std::uint64_t mLargestOther = 0;
    };

    /// `PrivateMemoryHeld` of this process, now, walked region by region, so asked once a stop and never a frame: what
    /// names the memory a Windows process holds past its heap. Nothing on Linux, where `anonymousMemory` and the heap's
    /// own report (`Platform::heapHeld`) already part the two, and where the system does not say.
    std::optional<PrivateMemoryHeld> privateMemoryHeld();
}
