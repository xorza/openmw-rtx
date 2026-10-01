#pragma once

#include <cstdint>
#include <string>
#include <string_view>

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

    /// Leaves the system nothing to keep of this process when it aborts: for a process that dies on
    /// purpose, as a death test's child does, whose core nobody wants.
    void disableCoreDump();
}
