#include "crashpadclientsystem.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <string_view>

#include "crashnote.hpp"

// Linux's alternate signal stack, which macOS has no call for.
#if !defined(__APPLE__)
#include <client/crashpad_client.h>
#endif

namespace Crash::Client
{
    namespace
    {
        void onHangSignal(int)
        {
            // **The interrupted code's `errno` is put back**: the report makes system calls of its
            // own, and a handler that returned with another left the call it interrupted reading a
            // cause it never had. ThreadSanitizer reports a handler that spoils it.
            const int interrupted = errno;
            reportHang();
            errno = interrupted;
        }

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

    void hookEveryEnd(Heartbeat&)
    {
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
