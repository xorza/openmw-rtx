#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "crashpage.hpp"

namespace crashpad
{
    class ExceptionSnapshot;
}

/// **The monitor's side of the catcher that each system spells its own way**, in
/// `crashpadmonitorposix.cpp` and `crashpadmonitorwin32.cpp`.
namespace Crash::Monitor
{
    /// The game itself, held from the monitor's start, so a hang request and an End reach the
    /// process that started the monitor and never one that took its id after it ended: a pidfd on
    /// Linux and a handle on Windows. macOS acts on the id.
    class GameProcess
    {
    public:
        explicit GameProcess(std::uint32_t id);
        GameProcess(const GameProcess&) = delete;
        GameProcess& operator=(const GameProcess&) = delete;
        ~GameProcess();

        /// Has the game write a hang report: a signal on POSIX, and on Windows, which has no signal
        /// to take it on, a thread of the game's own started at the function `page` names, as a
        /// debugger starts one; the game's frames are untouched.
        void requestHangReport(Heartbeat& page) const;

        /// Ends the game, and says whether it was there to be ended.
        bool end() const;

    private:
        std::uint32_t mId = 0;

        /// What the system holds the process by, a handle or a descriptor, and -1 for nothing.
        std::intptr_t mHold = -1;
    };

    /// The exception as the system names it, or nothing where the dump was asked for rather than
    /// raised by a fault. `process` is the game's id, by which a signal it sent itself is told from
    /// one another process sent.
    std::string describeException(const crashpad::ExceptionSnapshot& exception, std::uint32_t process);

    /// The command line as UTF-8, which is what Crashpad's own entry hands `HandlerMain`: on Windows
    /// from the wide one, because `argv` is in the system's code page there.
    std::vector<std::string> commandLine(int argc, char** argv);

    std::tm localTime(std::time_t seconds);

    /// The folder in Crashpad's database that each system's handler leaves a finished dump in.
    std::string_view dumpFolder();

    inline std::string hex(std::uint64_t value)
    {
        char text[20];
        std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
        return text;
    }

    using Names = std::span<const std::pair<std::uint32_t, std::string_view>>;

    /// `code`'s name in `names`, or `otherwise` where it has none.
    inline std::string nameOf(Names names, std::uint32_t code, std::string otherwise)
    {
        const auto named = std::find_if(names.begin(), names.end(), [&](const auto& one) { return one.first == code; });
        return named != names.end() ? std::string(named->second) : std::move(otherwise);
    }
}
