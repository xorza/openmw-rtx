#include "processhold.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <limits>

#include <poll.h>
// NOLINTNEXTLINE(modernize-deprecated-headers): POSIX's `SIGKILL`, which <csignal> need not define
#include <signal.h> // IWYU pragma: keep: `SIGKILL`, which glibc defines in a bits header nothing maps to this one
#include <sys/syscall.h>
#include <unistd.h>

namespace Platform
{
    namespace
    {
        // `pidfd_open`'s number, which every architecture shares from Linux 5.3 on: AlmaLinux 8's kernel headers,
        // which the release builds against, name `pidfd_send_signal`, backported, and no `SYS_pidfd_open`. A
        // kernel older than 5.3 answers ENOSYS, as it would to the named one.
        // NOLINTNEXTLINE(google-runtime-int): syscall's number type
        constexpr long sPidfdOpen = 434;
    }

    ProcessHold::ProcessHold(std::uint32_t id)
        : mId(id)
    {
        // Through `syscall`, since the C library does not wrap a pidfd everywhere.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): Linux's syscall
        mHold = UniqueHold<Closing>(syscall(sPidfdOpen, static_cast<pid_t>(id), 0));
    }

    void ProcessHold::Closing::close(const Handle hold) noexcept
    {
        ::close(static_cast<std::int32_t>(hold));
    }

    ProcessEnd ProcessHold::end() const
    {
        // No descriptor is a kernel older than 5.3, where nothing is sent rather than something sent to whatever
        // process has the id now.
        if (!mHold.isOpen())
            return ProcessEnd::Failed;
        // A signal to a process that ended and is not yet reaped is taken, as Windows' end of one is not.
        if (hasEnded())
            return ProcessEnd::Gone;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): Linux's syscall
        if (syscall(SYS_pidfd_send_signal, static_cast<std::int32_t>(mHold.get()), SIGKILL, nullptr, 0) == 0)
            return ProcessEnd::Ended;
        return errno == ESRCH ? ProcessEnd::Gone : ProcessEnd::Failed;
    }

    bool ProcessHold::waitForEnd(std::chrono::milliseconds most) const
    {
        assert(most >= std::chrono::milliseconds::zero());
        if (!mHold.isOpen())
            return true;
        pollfd polled{ .fd = static_cast<std::int32_t>(mHold.get()), .events = POLLIN, .revents = 0 };
        // A signal cuts a wait short, and what is left of `most` is waited out, which runs out, so the loop ends.
        // Counted from the start rather than to a deadline, which `most` near its type's limit would overflow.
        const auto start = std::chrono::steady_clock::now();
        for (;;)
        {
            const auto left = most
                - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
            const std::int32_t wait = static_cast<std::int32_t>(
                std::clamp<std::int64_t>(left.count(), 0, std::numeric_limits<std::int32_t>::max()));
            const std::int32_t ready = poll(&polled, 1, wait);
            if (ready >= 0)
                return ready != 0;
            if (errno != EINTR)
                return true;
        }
    }
}
