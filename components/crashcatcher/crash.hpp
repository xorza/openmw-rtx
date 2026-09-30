#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/// **The crash catcher**, the same on every system Crashpad supports: Windows, Linux and macOS.
///
/// A monitor process, this same executable started with `--crash-monitor`, reads a crashed,
/// hung or reporting game from outside: Crashpad writes a minidump of every thread, and the
/// monitor appends a summary to the game's log. Once the game is gone, the monitor zips the log and
/// the session's dumps into one file and a dialog names it. Nothing but Crashpad's own signal-safe
/// step runs in the crashed process.
namespace Crash
{
    struct Settings
    {
        /// Names the reports: "OpenMW".
        std::string mApplication;

        /// Where Crashpad keeps the reports: `crashes/` in the user data folder, where
        /// `OPENMW_CRASH_REPORTS` does not name one.
        std::filesystem::path mReportFolder;

        /// Whether a crash and a hang put up a dialog. A harness run from a shell does not want one.
        bool mDialog = true;

        /// **A harness's answer to the hang dialog, where nobody is at the box**: End, after this
        /// long, which is what lets a test end a game that recovered, or ended, while it was asked.
        /// Nothing asks the player.
        std::optional<std::chrono::milliseconds> mEndAfter;

        /// Where a player reports a crash, which the dialog names and opens. Empty for nowhere.
        std::string mIssues;
    };

    /// Runs the monitor and ends the process, where this process was started as one; returns
    /// otherwise. Called first in `main`, before anything else starts.
    void runMonitorIfAsked(int argc, char** argv);

    /// Starts the monitor and hooks every way this process can end in a crash. Nothing where it did,
    /// and why not where it did not: a system Crashpad does not support, or a monitor that would
    /// not start. Once in a process, as early as it can be: a crash before the log is set up is a
    /// crash all the same.
    std::optional<std::string> install(const Settings& settings);

    /// Where the monitor appends each summary: the game's own log, known once the configuration has
    /// been read, which is after `install`. Before this, a summary is in its dump alone. Nothing
    /// where no catcher is installed.
    void setLogFile(const std::filesystem::path& log);

    /// How long without a heartbeat is a hang; nought, as it is until this is called, turns the
    /// check off. The watch begins at the first heartbeat, so a start that draws nothing for a
    /// while is no hang.
    void setHangLimit(std::chrono::seconds limit);

    /// Once for each frame the game draws, a loading screen's included: one relaxed store.
    void heartbeat();

    /// A key and a value every later report carries: the version, the renderer, the device.
    void annotate(std::string_view key, std::string_view value);

    /// **Ends the process as a crash**, for a failure nothing can go on from: every thread is dumped
    /// where the failure was found, before anything unwinds, and `reason` heads the summary, cut to
    /// what a note holds. What is longer the caller logs first. Without a catcher, `reason` goes to
    /// the standard error and the process aborts all the same.
    [[noreturn]] void fatal(std::string_view reason);

    /// A contract the code keeps, said once for every build: one compare and a cold call that never
    /// returns, which ends the process as a crash whose report names `what`. Not a trap, which the
    /// builds players run would report as `SIGILL` and nothing else, and not
    /// `__builtin_unreachable`: GCC's `-Wnull-dereference` still names the path an unreachable
    /// rules out, and a violated contract would then run on into whatever it dereferenced. A path
    /// no contract compare guards, such as the end of a switch that names every case, calls `fatal`
    /// itself. Never for what the world might supply: a contract is the code's, and untrusted input
    /// is a throw.
    inline void contract(const bool held, const char* what)
    {
        if (!held)
            fatal(what);
    }

    /// `pointer`, which the code holds is never null, as `contract` holds it: for a getter that may
    /// answer null in general and cannot where it is called. Its own compare rather than a call to
    /// `contract`: at `-O3` GCC did not inline that far, and read the pointer returned as maybe null.
    template <class T>
    T* notNull(T* pointer, const char* what)
    {
        if (pointer == nullptr)
            fatal(what);
        return pointer;
    }

    /// A report without a crash, for a contract broken where the game can go on: a dump of every
    /// thread and a summary, and the game continues. Nothing where no catcher is installed.
    void report(std::string_view reason);
}
