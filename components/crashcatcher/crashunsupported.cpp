#include "crash.hpp"
#include "crashinstall.hpp"

#include <chrono>
#include <filesystem>
#include <string_view>

#include <components/misc/result.hpp>

#include "crashuncaught.hpp"

// **No crash catcher where this build has no Crashpad**: a system Crashpad does not run on, FreeBSD
// among them, or one built with `OPENMW_CRASHPAD=OFF`. `install` says so, and the log carries it.
// Notes are still taken, because `crashnote.cpp` is the same everywhere.
namespace Crash
{
    void runMonitorIfAsked(int, char**) {}

    Misc::Result<Installed, std::string_view> install(const Settings&)
    {
        return Misc::Err{ "this build has no Crashpad" };
    }

    void setLogFile(const std::filesystem::path&) {}

    void setReportFolder(const std::filesystem::path&) {}

    void setHangLimit(std::chrono::seconds) {}

    void heartbeat() {}

    void annotate(std::string_view, std::string_view) {}

    void report(std::string_view) {}

    void fatal(std::string_view reason)
    {
        abortUncaught(reason);
    }
}
