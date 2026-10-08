#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

#include <components/misc/result.hpp>

#include "crashanswering.hpp"

/// **Starting the crash catcher**, which `main` and the log's setup do once: apart from `crash.hpp`,
/// so that the hundreds of files that only report or die through it compile none of this.
namespace Crash
{
    struct Settings
    {
        /// Names the reports: "OpenMW".
        std::string mApplication;

        /// Where Crashpad keeps the reports: `crashes/` in the user data folder, where
        /// `OPENMW_CRASH_REPORTS` does not name one.
        std::filesystem::path mReportFolder;

        /// Who answers a hang's box and a crash's: a harness run from a shell wants neither.
        Answering mAnswering = AskThePlayer{};

        /// Where a player reports a crash, which the dialog names and opens. Empty for nowhere.
        std::string mIssues;
    };

    /// What a started catcher goes without, said for the log: nothing, or what it could not hook and
    /// why.
    struct Installed
    {
        std::string_view mWithout;
    };

    /// Runs the monitor and ends the process, where this process was started as one; returns
    /// otherwise. Called first in `main`, before anything else starts.
    void runMonitorIfAsked(int argc, char** argv);

    /// **Under an AppImage, keeps the image mounted while a process it started runs a program from
    /// it**, the monitors among them: this process forks, and returns in the child, which goes on as
    /// the application; the parent holds the mount, passes a termination on, and ends as the
    /// application did once none is left. Returns at once anywhere else. Called while the process
    /// has one thread, which is all a fork keeps, and before `install`, so the monitor is the
    /// application's. Says why the image is not kept, where it is due and could not be, and nothing
    /// otherwise.
    std::string_view keepImageMounted();

    /// Starts the monitor and hooks every way this process can end in a crash. Whether it did, and
    /// why not where it did not: a system Crashpad does not support, or a monitor that would
    /// not start. Once in a process, as early as it can be: a crash before the log is set up is a
    /// crash all the same.
    Misc::Result<Installed, std::string_view> install(const Settings& settings);

    /// Where the monitor appends each summary: the game's own log, known once the configuration has
    /// been read, which is after `install`. Before this, a summary is in its dump alone. Nothing
    /// where no catcher is installed.
    void setLogFile(const std::filesystem::path& log);

    /// Where the monitor writes the session's package: the folder the player's configuration names,
    /// known once it has been read, which is after `install`. Before this, and where a harness named
    /// one through `OPENMW_CRASH_REPORTS`, the package goes beside Crashpad's dumps. Nothing where
    /// no catcher is installed.
    void setReportFolder(const std::filesystem::path& folder);

    /// How long without a heartbeat is a hang; nought, as it is until this is called, turns the
    /// check off, and a limit set again counts from then and not from the last heartbeat. The watch
    /// begins at the first heartbeat, so a start that draws nothing for a while is no hang.
    void setHangLimit(std::chrono::seconds limit);

    /// What `setHangLimit` last set; nought where no catcher is installed.
    std::chrono::seconds getHangLimit();

    /// A stretch the thread that draws spends drawing nothing by design — a wait for work it chose
    /// to have whole before its next frame — and so no hang: the watch is off while one stands, and
    /// counts afresh from its end.
    class HangPause
    {
    public:
        HangPause()
            : mLimit(getHangLimit())
        {
            setHangLimit(std::chrono::seconds(0));
        }

        ~HangPause() { setHangLimit(mLimit); }

        HangPause(const HangPause&) = delete;
        HangPause& operator=(const HangPause&) = delete;

    private:
        const std::chrono::seconds mLimit;
    };
}
