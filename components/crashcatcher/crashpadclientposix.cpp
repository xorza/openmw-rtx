#include "crashpadclientsystem.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <string_view>

#include <fcntl.h>

#include <client/crashpad_client.h>

#include "crashnote.hpp"

namespace Crash::Client
{
    namespace
    {
        /// **A fault waits out a report another thread is writing**, which it would write over: the
        /// client keeps one exception record, and the report's dump was summarised as the fault on
        /// the reporting thread. Bounded, because a report that never ends must not hold a crash:
        /// two seconds, against the few hundred milliseconds a dump takes. Then Crashpad's own
        /// handler takes the fault.
        bool onFault(int, siginfo_t*, ucontext_t*)
        {
            const int interrupted = errno;
            constexpr timespec pause{ .tv_sec = 0, .tv_nsec = 1'000'000 };
            for (int waited = 0; takeForFault() == FaultGate::Other && waited < 2000; ++waited)
                nanosleep(&pause, nullptr);
            errno = interrupted;
            return false;
        }
    }

    void keepConnectionToThisProcess()
    {
        // `StartHandler` makes the pair with `socketpair` and no `SOCK_CLOEXEC`.
        int socket = -1;
        pid_t monitor = 0;
        if (crashpad::CrashpadClient::GetHandlerSocket(&socket, &monitor))
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
        crashpad::CrashpadClient::SetFirstChanceExceptionHandler(onFault);
    }

    void endAsCrash(std::string_view reason)
    {
        // Crashpad's own handler takes the abort, as it takes every fatal signal.
        finalReport(ReportKind::Crash, reason);
        std::abort();
    }
}
