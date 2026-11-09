#include "crashpadmonitorsystem.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <snapshot/exception_snapshot.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <components/platform/wholefile.hpp>

#include "crashsummary.hpp"

namespace Crash::Monitor
{
    GameProcess::GameProcess(std::uint32_t id)
        : mId(id)
    {
        // Through `syscall`, since the C library does not wrap a pidfd everywhere.
        mHold = Platform::UniqueHold<Closing>(syscall(SYS_pidfd_open, static_cast<pid_t>(id), 0));
    }

    void GameProcess::Closing::close(const Handle hold) noexcept
    {
        ::close(static_cast<int>(hold));
    }

    Ending GameProcess::end() const
    {
        // No descriptor is a kernel older than 5.3, where nothing is sent rather than something sent
        // to whatever process has the id now.
        if (mHold.get() < 0)
            return Ending::Failed;
        if (syscall(SYS_pidfd_send_signal, static_cast<int>(mHold.get()), SIGKILL, nullptr, 0) == 0)
            return Ending::Ended;
        return errno == ESRCH ? Ending::Gone : Ending::Failed;
    }

    std::optional<std::uint32_t> GameProcess::exitCode() const
    {
        return std::nullopt;
    }

    bool GameProcess::isHeld() const
    {
        // The state follows the name in parentheses, which may hold anything, so after the last of
        // them: `T` stopped, `t` stopped by a tracer.
        const std::string folder = "/proc/" + std::to_string(mId);
        std::string text;
        if (Platform::readWhole(folder + "/stat", text).isOk())
        {
            const std::size_t named = text.rfind(')');
            if (named != std::string::npos && named + 2 < text.size()
                && (text[named + 2] == 'T' || text[named + 2] == 't'))
                return true;
        }

        // A tracer that lets the game run stands at a breakpoint as often as not.
        if (!Platform::readWhole(folder + "/status", text).isOk())
            return false;
        constexpr std::string_view tracer = "\nTracerPid:";
        const std::size_t at = text.find(tracer);
        return at != std::string::npos && std::strtol(text.c_str() + at + tracer.size(), nullptr, 10) != 0;
    }

    std::string describeExitCode(std::uint32_t code)
    {
        return hex(code);
    }

    std::string describeException(const crashpad::ExceptionSnapshot& exception, std::uint32_t process)
    {
        const std::uint32_t code = exception.Exception();
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
    }
}
