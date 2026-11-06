#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <components/platform/uniquehold.hpp>

namespace crashpad
{
    class ExceptionSnapshot;
}

/// **The monitor's side of the catcher that each system spells its own way**, in
/// `crashpadmonitorposix.cpp` and `crashpadmonitorwin32.cpp`.
namespace Crash::Monitor
{
    /// What an End came to.
    enum class Ending
    {
        Ended,

        /// The game was gone before the monitor ended it.
        Gone,

        /// The monitor holds nothing to end it by, or the system refused.
        Failed,
    };

    /// The game itself, held from the monitor's start, so an End reaches the process that started
    /// the monitor and never one that took its id after it ended: a pidfd on Linux and a handle on
    /// Windows.
    class GameProcess
    {
    public:
        explicit GameProcess(std::uint32_t id);

        /// Ends the game, and says what that came to.
        Ending end() const;

        /// Whether something holds the game still that is not the game: a debugger, or on POSIX a
        /// stop signal from a shell. A stall it spends so is no hang.
        bool isHeld() const;

        /// What the game exited with, once it is gone, where the system tells a process that is
        /// not its parent: Windows does, and a POSIX system tells the parent alone.
        std::optional<std::uint32_t> exitCode() const;

    private:
        std::uint32_t mId = 0;

        /// What the system holds the process by, a handle or a descriptor, and -1 for nothing.
        struct Closing
        {
            using Handle = std::intptr_t;
            static constexpr Handle sNone = -1;
            static void close(Handle hold) noexcept;
        };

        Platform::UniqueHold<Closing> mHold;
    };

    /// The exception as the system names it, or nothing where the dump was asked for rather than
    /// raised by a fault. `process` is the game's id, by which a signal it sent itself is told from
    /// one another process sent.
    std::string describeException(const crashpad::ExceptionSnapshot& exception, std::uint32_t process);

    /// An exit code as the system names it, where it has a name, and in hex where it has none.
    std::string describeExitCode(std::uint32_t code);
}
