#include "crashpadmonitorsystem.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <snapshot/exception_snapshot.h>
#include <sys/types.h>
#include <unistd.h>

#include "crashsummary.hpp"

// Linux holds the game by a pidfd, which the C library has no wrapper for everywhere; macOS says
// whether a process is stopped or traced through `sysctl`.
#if defined(__linux__)
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace Crash::Monitor
{
    namespace
    {
        bool send(std::uint32_t id, std::intptr_t hold, int number)
        {
#if defined(__linux__) && defined(SYS_pidfd_send_signal)
            // No descriptor is a kernel older than 5.3, where nothing is sent rather than something
            // sent to whatever process has the id now.
            (void)id;
            if (hold < 0)
            {
                errno = EBADF;
                return false;
            }
            return syscall(SYS_pidfd_send_signal, static_cast<int>(hold), number, nullptr, 0) == 0;
#else
            (void)hold;
            return kill(static_cast<pid_t>(id), number) == 0;
#endif
        }
    }

    GameProcess::GameProcess(std::uint32_t id)
        : mId(id)
    {
#if defined(__linux__) && defined(SYS_pidfd_open)
        mHold = Platform::UniqueHold<Closing>(syscall(SYS_pidfd_open, static_cast<pid_t>(id), 0));
#endif
    }

    void GameProcess::Closing::close(const Handle hold) noexcept
    {
        ::close(static_cast<int>(hold));
    }

    void GameProcess::requestHangReport(Heartbeat&) const
    {
        send(mId, mHold.get(), SIGUSR2);
    }

    Ending GameProcess::end() const
    {
        if (send(mId, mHold.get(), SIGKILL))
            return Ending::Ended;
        return errno == ESRCH ? Ending::Gone : Ending::Failed;
    }

    std::optional<std::uint32_t> GameProcess::exitCode() const
    {
        return std::nullopt;
    }

    bool GameProcess::isHeld() const
    {
#if defined(__APPLE__)
        int name[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(mId) };
        kinfo_proc info{};
        std::size_t size = sizeof(info);
        if (sysctl(name, 4, &info, &size, nullptr, 0) != 0 || size == 0)
            return false;
        return (info.kp_proc.p_flag & P_TRACED) != 0 || info.kp_proc.p_stat == SSTOP;
#else
        // The state follows the name in parentheses, which may hold anything, so after the last of
        // them: `T` stopped, `t` stopped by a tracer.
        const std::string folder = "/proc/" + std::to_string(mId);
        std::ifstream stat(folder + "/stat");
        std::string line;
        std::getline(stat, line);
        const std::size_t named = line.rfind(')');
        if (named != std::string::npos && named + 2 < line.size() && (line[named + 2] == 'T' || line[named + 2] == 't'))
            return true;

        // A tracer that lets the game run stands at a breakpoint as often as not.
        std::ifstream status(folder + "/status");
        for (std::string field; std::getline(status, field);)
            if (field.starts_with("TracerPid:"))
                return std::strtol(field.c_str() + sizeof("TracerPid:") - 1, nullptr, 10) != 0;
        return false;
#endif
    }

    std::string describeExitCode(std::uint32_t code)
    {
        return hex(code);
    }

    std::string describeException(const crashpad::ExceptionSnapshot& exception, std::uint32_t process)
    {
        const std::uint32_t code = exception.Exception();
#if defined(__APPLE__)
        (void)process;

        // `kMachExceptionSimulated`, 'CPsx'.
        if (code == 0x43507378u)
            return {};

        static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 6> sNames{ {
            { 1, "EXC_BAD_ACCESS" },
            { 2, "EXC_BAD_INSTRUCTION" },
            { 3, "EXC_ARITHMETIC" },
            { 6, "EXC_BREAKPOINT" },
            { 10, "EXC_CRASH" },
            { 12, "EXC_GUARD" },
        } };
        std::string text = nameOf(sNames, code, "Mach exception " + std::to_string(code));
        if (code == 1)
            text += " at " + hex(exception.ExceptionAddress());
        return text;
#else
        if (code == 0xFFFFFFFFu)
            return {};

        static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 7> sNames{ {
            { SIGSEGV, "SIGSEGV" },
            { SIGBUS, "SIGBUS" },
            { SIGILL, "SIGILL" },
            { SIGFPE, "SIGFPE" },
            { SIGABRT, "SIGABRT" },
            { SIGTRAP, "SIGTRAP" },
            { SIGSYS, "SIGSYS" },
        } };
        std::string text = nameOf(sNames, code, "signal " + std::to_string(code));

        // A code of nought or less is a signal sent rather than a fault, `kill` or `raise`, whose
        // address is no fault's. Crashpad keeps the sender's id first among the codes of the signals
        // that carry one: the game's own, as `abort` sends it, says nothing more.
        if (static_cast<std::int32_t>(exception.ExceptionInfo()) <= 0)
        {
            const std::vector<std::uint64_t>& codes = exception.Codes();
            if (!codes.empty() && codes[0] != process)
                text += " sent by process " + std::to_string(codes[0]);
        }
        else if (code == SIGSEGV || code == SIGBUS)
            text += " at " + hex(exception.ExceptionAddress());
        return text;
#endif
    }

    std::string_view dumpFolder()
    {
        return "pending";
    }
}
