#include "crashpadmonitorsystem.hpp"

#include <array>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include <snapshot/exception_snapshot.h>
#include <sys/types.h>
#include <unistd.h>

// Linux holds the game by a pidfd, which the C library has no wrapper for everywhere.
#if defined(__linux__)
#include <sys/syscall.h>
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
            return hold >= 0 && syscall(SYS_pidfd_send_signal, static_cast<int>(hold), number, nullptr, 0) == 0;
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
        mHold = syscall(SYS_pidfd_open, static_cast<pid_t>(id), 0);
#endif
    }

    GameProcess::~GameProcess()
    {
        if (mHold >= 0)
            close(static_cast<int>(mHold));
    }

    void GameProcess::requestHangReport(Heartbeat&) const
    {
        send(mId, mHold, SIGUSR2);
    }

    bool GameProcess::end() const
    {
        return send(mId, mHold, SIGKILL);
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

    std::vector<std::string> commandLine(int argc, char** argv)
    {
        return std::vector<std::string>(argv, argv + argc);
    }

    std::tm localTime(std::time_t seconds)
    {
        std::tm local{};
        localtime_r(&seconds, &local);
        return local;
    }

    std::string_view dumpFolder()
    {
        return "pending";
    }
}
