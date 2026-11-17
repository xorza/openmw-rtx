#include <array>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <client/crashpad_client.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <components/platform/wholefile.hpp>

#include "crashnote.hpp"
#include "crashpadclientsystem.hpp"

namespace Crash::Client
{
    namespace
    {
        /// The fatal signals whose handler `lostHooks` checks is still Crashpad's, which installs one
        /// for each, by name.
        constexpr std::array<std::pair<std::int32_t, std::string_view>, 7> sHooked{ {
            { SIGSEGV, "SIGSEGV" },
            { SIGBUS, "SIGBUS" },
            { SIGFPE, "SIGFPE" },
            { SIGILL, "SIGILL" },
            { SIGABRT, "SIGABRT" },
            { SIGSYS, "SIGSYS" },
            { SIGTRAP, "SIGTRAP" },
        } };

        static_assert(sHooked.size() <= SystemState{}.mHooks.size());

        std::uintptr_t handlerOf(std::int32_t signal)
        {
            struct sigaction current{};
            sigaction(signal, nullptr, &current);
            return (current.sa_flags & SA_SIGINFO) != 0 ? reinterpret_cast<std::uintptr_t>(current.sa_sigaction)
                                                        : reinterpret_cast<std::uintptr_t>(current.sa_handler);
        }

        /// The connection to the monitor, or -1 before there is one.
        std::int32_t handlerSocket()
        {
            std::int32_t socket = -1;
            pid_t monitor = 0;
            return crashpad::CrashpadClient::GetHandlerSocket(&socket, &monitor) ? socket : -1;
        }
    }

    void keepConnectionToThisProcess()
    {
        // `StartHandler` makes the pair with `socketpair` and no `SOCK_CLOEXEC`.
        if (const std::int32_t socket = handlerSocket(); socket >= 0)
            fcntl(socket, F_SETFD, fcntl(socket, F_GETFD) | FD_CLOEXEC);
    }

    void prepareInstallingThread()
    {
        // Nothing: Crashpad's install gives the installing thread its signal stack
        // (`SignalHandler::Install`), as `pthread_create_linux.cc` gives every thread made after.
    }

    std::string_view catchPastTheProcess(crashpad::CrashpadClient&, const std::filesystem::path&)
    {
        // A signal reaches the handler whatever raised it.
        return {};
    }

    void hookEveryEnd()
    {
        // Nothing to add: Crashpad's handler takes every fatal signal. What it set is kept.
        std::array<std::uintptr_t, 8>& hooks = systemState().mHooks;
        for (std::size_t at = 0; at < sHooked.size(); ++at)
            hooks[at] = handlerOf(sHooked[at].first);
    }

    void lostHooks(std::vector<std::string_view>& into)
    {
        const std::array<std::uintptr_t, 8>& hooks = systemState().mHooks;
        for (std::size_t at = 0; at < sHooked.size(); ++at)
            if (handlerOf(sHooked[at].first) != hooks[at])
                into.push_back(sHooked[at].second);
    }

    namespace
    {
        /// Yama's `ptrace_scope`, or nothing where the system has no Yama: then a process reads
        /// what its user's others are.
        std::optional<std::int32_t> yamaScope()
        {
            std::string text;
            if (!Platform::readWhole("/proc/sys/kernel/yama/ptrace_scope", text, 64).isOk())
                return std::nullopt;
            std::int32_t scope = 0;
            if (std::from_chars(text.data(), text.data() + text.size(), scope).ec != std::errc{})
                return std::nullopt;
            return scope;
        }
    }

    std::string describeReading()
    {
        const std::optional<std::int32_t> scope = yamaScope();
        return scope.has_value() ? "Yama's ptrace_scope " + std::to_string(*scope) : "no Yama";
    }

    std::string_view unreadable()
    {
        const std::int32_t scope = yamaScope().value_or(0);
        if (scope >= 3)
            return "Yama's ptrace_scope is 3, which lets no process read another";
        if (scope == 2 && geteuid() != 0)
            return "Yama's ptrace_scope is 2, which lets only an administrator's process read another";
        return {};
    }

    bool monitorRuns()
    {
        // The far end of the pair is the monitor's alone, so it closes when the monitor ends.
        const std::int32_t socket = handlerSocket();
        if (socket < 0)
            return false;
        pollfd polled{ .fd = socket, .events = POLLRDHUP, .revents = 0 };
        return poll(&polled, 1, 0) == 0;
    }

    void watchMonitor(std::uint32_t) {}

    std::string_view letMonitorRead(std::uint32_t process)
    {
        // Linux without Yama answers EINVAL: any process of the user's reads another already.
        // NOLINTNEXTLINE(google-runtime-int): prctl's argument type
        if (prctl(PR_SET_PTRACER, static_cast<unsigned long>(process), 0, 0, 0) != 0 && errno != EINVAL)
            return "a hang's dump: the system would not let the monitor read the game";
        return {};
    }

    void endAsCrash(std::string_view reason)
    {
        // Crashpad's own handler takes the abort, as it takes every fatal signal, where its monitor runs.
        if (!monitorRuns())
            sayUnreported(reason);
        endingReason(reason);
        std::abort();
    }
}
