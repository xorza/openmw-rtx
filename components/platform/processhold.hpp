#pragma once

#include <chrono>
#include <cstdint>

#include "uniquehold.hpp"

namespace Platform
{
    /// What ending a held process came to.
    enum class ProcessEnd : std::uint8_t
    {
        Ended,

        /// The process was gone before it was ended.
        Gone,

        /// Nothing is held to end it by, or the system refused.
        Failed,
    };

    /// **A process held by what the system never hands to another**: a pidfd on Linux (`pidfd_open(2)`), a handle on
    /// Windows (`OpenProcess`), taken while the process stands, so an end and a wait reach that process and never one
    /// that took its id after it ended. Over `processholdlinux.cpp` and `processholdwin32.cpp`. On Windows with the
    /// rights to read the process whole as well, which the crash monitor's dump of a hang reads it through.
    class ProcessHold
    {
    public:
        /// Holds the process `id`, or nothing where it is gone, the system refused, or Linux is older than 5.3.
        explicit ProcessHold(std::uint32_t id);

        std::uint32_t id() const { return mId; }

        bool isOpen() const { return mHold.isOpen(); }

        /// The descriptor or the handle itself, for a system's own file beside this one, and -1 where nothing is held.
        std::intptr_t native() const { return mHold.get(); }

        /// Ends the process at once, past every handler of its own: `SIGKILL`, or `TerminateProcess` with 3, the code
        /// `abort` leaves there.
        ProcessEnd end() const;

        /// **Whether the process has ended**, waiting up to `most` for it. On Linux once its last thread has, as a
        /// pidfd reads ready (the kernel's `pidfd_poll`): a killed process's first thread ends before the others, and
        /// its files close with the last. A hold of nothing answers yes, as does a system that would not say.
        bool waitForEnd(std::chrono::milliseconds most) const;

        /// Whether the process has ended, now.
        bool hasEnded() const { return waitForEnd(std::chrono::milliseconds::zero()); }

    private:
        struct Closing
        {
            using Handle = std::intptr_t;
            static constexpr Handle sNone = -1;
            static void close(Handle hold) noexcept;
        };

        std::uint32_t mId = 0;
        UniqueHold<Closing> mHold;
    };
}
