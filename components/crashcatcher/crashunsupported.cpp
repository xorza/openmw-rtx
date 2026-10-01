#include <chrono>
#include <filesystem>
#include <string_view>

#include <components/misc/result.hpp>

#include "crash.hpp"
#include "crashinstall.hpp"

#include "crashuncaught.hpp"

// **No crash catcher where Crashpad does not run**, FreeBSD among them: `install` says so, and the
// log carries it. Notes are still taken, because `crashnote.cpp` is the same everywhere.
namespace Crash
{
    void runMonitorIfAsked(int, char**) {}

    Misc::Result<void, std::string_view> install(const Settings&)
    {
        return Misc::Err{ "Crashpad does not support this system" };
    }

    void setLogFile(const std::filesystem::path&) {}

    void setHangLimit(std::chrono::seconds) {}

    void heartbeat() {}

    void annotate(std::string_view, std::string_view) {}

    void report(std::string_view) {}

    void fatal(std::string_view reason)
    {
        abortUncaught(reason);
    }
}
