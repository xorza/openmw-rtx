#include "processhold.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>

#include <poll.h>
// NOLINTNEXTLINE(modernize-deprecated-headers): POSIX's `SIGKILL` and `kill`, which <csignal> need not define
#include <signal.h> // IWYU pragma: keep: `SIGKILL`, which glibc defines in a bits header nothing maps to this one
#include <sys/syscall.h>
#include <unistd.h>

#include "kernelfilelinux.hpp"
#include "linuxtext.hpp"
#include "processholdlinux.hpp"

namespace Platform
{
    namespace
    {
        // `pidfd_open`'s number, which every architecture shares from Linux 5.3 on: AlmaLinux 8's kernel headers,
        // which the release builds against, name `pidfd_send_signal`, backported, and no `SYS_pidfd_open`. A
        // kernel older than 5.3 answers ENOSYS, as it would to the named one.
        // NOLINTNEXTLINE(google-runtime-int): syscall's number type
        constexpr long sPidfdOpen = 434;

        /// How often a hold by start time asks the process's `stat` whether it ended: the crash monitor's watch asks
        /// once a second, and an end it waits on is a kill's.
        constexpr std::chrono::milliseconds sStatPoll{ 10 };

        /// What the process `id`'s `stat` states, or nothing where it cannot be read: a process reaped, whose entry
        /// is gone or fails a read opened before the reap with `ESRCH`.
        std::optional<LinuxText::ProcessStat> statOf(std::uint32_t id)
        {
            // A path of at most 21 characters and its terminator, and a `stat` the kernel writes in a few hundred
            // bytes, its name at most fifteen.
            std::array<char, 32> path{};
            std::format_to_n(path.data(), path.size() - 1, "/proc/{}/stat", id);
            std::array<char, 1024> buffer{};
            const std::optional<std::string_view> text = KernelFile::read(path.data(), buffer);
            if (!text.has_value())
                return std::nullopt;
            return LinuxText::processStat(*text);
        }

        /// Whether `stat` states the process started at `started` ended: gone, another process under its id, or a
        /// zombie whose last thread has ended, which is when a pidfd reads ready.
        bool endedAt(const std::optional<LinuxText::ProcessStat>& stat, std::uint64_t started)
        {
            return !stat.has_value() || stat->mStartTicks != started
                || ((stat->mState == 'Z' || stat->mState == 'X') && stat->mThreads <= 1);
        }
    }

    ProcessHold holdByStartTime(std::uint32_t id)
    {
        ProcessHold hold;
        hold.mId = id;
        const std::optional<LinuxText::ProcessStat> stat = statOf(id);
        if (stat.has_value() && !endedAt(stat, stat->mStartTicks))
            hold.mHeld = ProcessHold::StartTicks{ .mTicks = stat->mStartTicks };
        return hold;
    }

    ProcessHold::ProcessHold(std::uint32_t id)
        : mId(id)
    {
        // Through `syscall`, since the C library does not wrap a pidfd everywhere.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): Linux's syscall
        UniqueHold<Closing> pidfd(syscall(sPidfdOpen, static_cast<pid_t>(id), 0));
        const std::int32_t refused = errno;
        if (pidfd.isOpen())
            mHeld = std::move(pidfd);
        // A process gone is held by nothing; one the kernel opens no pidfd for, below 5.3 or out of descriptors,
        // by its start time.
        else if (refused != ESRCH)
            mHeld = std::move(holdByStartTime(id).mHeld);
    }

    void ProcessHold::Closing::close(const Handle hold) noexcept
    {
        ::close(static_cast<std::int32_t>(hold));
    }

    ProcessEnd ProcessHold::end() const
    {
        if (!isOpen())
            return ProcessEnd::Failed;
        // A signal to a process that ended and is not yet reaped is taken, as Windows' end of one is not.
        if (hasEnded())
            return ProcessEnd::Gone;
        // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg): Linux's syscall
        const bool sent = native() != Closing::sNone
            ? syscall(SYS_pidfd_send_signal, static_cast<std::int32_t>(native()), SIGKILL, nullptr, 0) == 0
            : kill(static_cast<pid_t>(mId), SIGKILL) == 0;
        // NOLINTEND(cppcoreguidelines-pro-type-vararg)
        if (sent)
            return ProcessEnd::Ended;
        return errno == ESRCH ? ProcessEnd::Gone : ProcessEnd::Failed;
    }

    bool ProcessHold::waitForEnd(std::chrono::milliseconds most) const
    {
        assert(most >= std::chrono::milliseconds::zero());
        if (!isOpen())
            return true;

        // A signal cuts a poll short, and what is left of `most` is waited out, which runs out, so the loop ends.
        // Counted from the start rather than to a deadline, which `most` near its type's limit would overflow.
        const auto start = std::chrono::steady_clock::now();
        const auto left = [&] {
            return most
                - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        };

        if (const StartTicks* const started = std::get_if<StartTicks>(&mHeld))
            for (;;)
            {
                if (endedAt(statOf(mId), started->mTicks))
                    return true;
                const std::chrono::milliseconds remaining = left();
                if (remaining <= std::chrono::milliseconds::zero())
                    return false;
                std::this_thread::sleep_for(std::min(remaining, sStatPoll));
            }

        pollfd polled{ .fd = static_cast<std::int32_t>(native()), .events = POLLIN, .revents = 0 };
        for (;;)
        {
            const std::int32_t wait = static_cast<std::int32_t>(
                std::clamp<std::int64_t>(left().count(), 0, std::numeric_limits<std::int32_t>::max()));
            const std::int32_t ready = poll(&polled, 1, wait);
            if (ready >= 0)
                return ready != 0;
            if (errno != EINTR)
                return true;
        }
    }
}
