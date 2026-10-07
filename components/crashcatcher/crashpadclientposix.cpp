#include "crashpadclientsystem.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <string_view>

#include "crashnote.hpp"

// Linux's alternate signal stack, which macOS has no call for; and macOS's pipe to the thread that
// takes a hang report.
#if !defined(__APPLE__)
#include <client/crashpad_client.h>
#else
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#endif

namespace Crash::Client
{
    namespace
    {
#if !defined(__APPLE__)
        void onHangSignal(int)
        {
            // **The interrupted code's `errno` is put back**: the report makes system calls of its
            // own, and a handler that returned with another left the call it interrupted reading a
            // cause it never had. ThreadSanitizer reports a handler that spoils it.
            const int interrupted = errno;
            reportHang();
            errno = interrupted;
        }
#else
        /// The pipe a hang request crosses, from the handler to the thread that takes the report.
        int sHangPipe[2] = { -1, -1 };

        /// **On macOS the handler only asks**: Crashpad's simulated crash builds a Mach message and
        /// allocates, which no signal handler may, on whichever thread the kernel handed the
        /// signal. One byte down a pipe is what a handler may do, and the reporter takes the report
        /// on a thread of its own, as the monitor's thread does on Windows. Never blocking: a pipe
        /// full of requests already asks for a report.
        void onHangSignal(int)
        {
            const int interrupted = errno;
            const char asked = 1;
            [[maybe_unused]] const ssize_t written = write(sHangPipe[1], &asked, 1);
            errno = interrupted;
        }

        void takeHangReports()
        {
            for (;;)
            {
                char asked = 0;
                const ssize_t got = read(sHangPipe[0], &asked, 1);
                if (got == 1)
                    reportHang();
                else if (got < 0 && errno == EINTR)
                    continue;
                else
                    return;
            }
        }
#endif

#if !defined(__APPLE__)
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
#endif
    }

    void prepareInstallingThread()
    {
#if !defined(__APPLE__)
        // Every thread made after this gets its own through `pthread_create_linux.cc`; the one
        // installing is older than that.
        crashpad::CrashpadClient::InitializeSignalStackForThread();
#endif
    }

    std::string_view catchPastTheProcess(crashpad::CrashpadClient&, const std::filesystem::path&)
    {
        // A signal reaches the handler whatever raised it.
        return {};
    }

    void hookEveryEnd(Heartbeat&)
    {
#if defined(__APPLE__)
        if (pipe(sHangPipe) == 0)
        {
            for (const int end : sHangPipe)
                fcntl(end, F_SETFD, FD_CLOEXEC);
            fcntl(sHangPipe[1], F_SETFL, O_NONBLOCK);
            std::thread(takeHangReports).detach();
        }
#endif

        // The monitor asks with `SIGUSR2`, on the thread's own stack and not the alternate one
        // Crashpad sizes for its own fault handler: a request arrives on a sound stack, and a whole
        // dump taken inside a signal frame outgrows the alternate one where the processor's saved
        // state makes the frame large.
        struct sigaction action = {};
        action.sa_handler = onHangSignal;
        action.sa_flags = SA_RESTART;
        sigemptyset(&action.sa_mask);
        sigaction(SIGUSR2, &action, nullptr);

#if !defined(__APPLE__)
        crashpad::CrashpadClient::SetFirstChanceExceptionHandler(onFault);
#endif
    }

    void endAsCrash(std::string_view reason)
    {
        // **The hang request blocked on this thread first**, so it cannot land here between the
        // report saying what it is and the abort; on another thread it finds the gate taken.
        // Crashpad's own handler takes the abort, as it takes every fatal signal.
        sigset_t hang;
        sigemptyset(&hang);
        sigaddset(&hang, SIGUSR2);
        pthread_sigmask(SIG_BLOCK, &hang, nullptr);

        finalReport(ReportKind::Crash, reason);
        std::abort();
    }
}
