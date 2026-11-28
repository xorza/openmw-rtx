#pragma once

#include <chrono>
#include <cstdint>
#include <variant>

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
    ///
    /// **Where Linux holds no pidfd, by the id and the time the process started** (`holdByStartTime`), as procps and
    /// polkit tell a process from the next to take its id: below 5.3, which the release's AlmaLinux 8 floor runs. A
    /// wait then polls, and an end checks the start time before it signals, a window of microseconds a reused id
    /// would have to fall into.
    class ProcessHold
    {
    public:
        /// Holds the process `id`, or nothing where it is gone or the system refused.
        explicit ProcessHold(std::uint32_t id);

        std::uint32_t id() const { return mId; }

        bool isOpen() const { return !std::holds_alternative<std::monostate>(mHeld); }

        /// The descriptor or the handle itself, for a system's own file beside this one, and -1 where none is held:
        /// where nothing is, or Linux holds it by its start time.
        std::intptr_t native() const
        {
            const UniqueHold<Closing>* const handle = std::get_if<UniqueHold<Closing>>(&mHeld);
            return handle != nullptr ? handle->get() : Closing::sNone;
        }

        /// Ends the process at once, past every handler of its own: `SIGKILL`, or `TerminateProcess` with 3, the code
        /// `abort` leaves there.
        ProcessEnd end() const;

        /// **Whether the process has ended**, waiting up to `most` for it. On Linux once its last thread has, as a
        /// pidfd reads ready (the kernel's `pidfd_poll`) and its `stat` states a zombie of one thread: a killed
        /// process's first thread ends before the others, and its files close with the last. A hold of nothing, which
        /// a process gone before it was held leaves, answers yes, as does a system that would not say.
        bool waitForEnd(std::chrono::milliseconds most) const;

        /// Whether the process has ended, now.
        bool hasEnded() const { return waitForEnd(std::chrono::milliseconds::zero()); }

    private:
        friend ProcessHold holdByStartTime(std::uint32_t id);

        ProcessHold() = default;

        struct Closing
        {
            using Handle = std::intptr_t;
            static constexpr Handle sNone = -1;
            static void close(Handle hold) noexcept;
        };

        /// When the process started, in the kernel's clock ticks after the boot: what Linux holds it by, with its
        /// id, where it opens no pidfd.
        struct StartTicks
        {
            std::uint64_t mTicks = 0;
        };

        std::uint32_t mId = 0;

        /// What holds the process: nothing where it was gone or the system refused, a handle or a pidfd, or on Linux
        /// below 5.3 its start time.
        std::variant<std::monostate, UniqueHold<Closing>, StartTicks> mHeld;
    };
}
