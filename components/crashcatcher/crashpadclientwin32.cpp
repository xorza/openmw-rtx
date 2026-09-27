#include "crashpadclientsystem.hpp"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

#include <client/simulate_crash.h>

#include <components/misc/windows.hpp>

#include "crashnote.hpp"

namespace Crash::Client
{
    namespace
    {
        /// A crash reported the way the system's own would not be: Crashpad dumps the process as it
        /// stands and returns, and the process ends here.
        [[noreturn]] void reportAndEnd(std::string_view reason)
        {
            finalReport(ReportKind::Crash, reason);
            CRASHPAD_SIMULATE_CRASH();
            std::_Exit(3);
        }

        // Three ways MSVC's runtime ends a process without an exception the filter sees: `abort`
        // raises SIGABRT and then fails fast, and a pure virtual call and an invalid parameter go
        // to handlers of their own.
        void onAbort(int)
        {
            reportAndEnd("abort()");
        }

        void onPureCall()
        {
            reportAndEnd("a pure virtual function was called");
        }

        void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
        {
            reportAndEnd("the C runtime was given an invalid parameter");
        }

        /// **Stack a thread keeps for its own stack overflow**, Windows's counterpart of the alternate
        /// signal stack. The overflow is dispatched on the thread whose stack is spent, and where the
        /// dispatch and Crashpad's filter need more than the guard page left, the thread faults again
        /// and the process ends with no dump: the matrix's stack overflow on the main thread did, one
        /// run in three. What `SetThreadStackGuarantee` reserves is there for exactly that.
        void guaranteeStack()
        {
            ULONG guarantee = 64 * 1024;
            SetThreadStackGuarantee(&guarantee);
        }

        /// Where the monitor starts a thread of the game's own to ask for a hang report: Windows has
        /// no signal to take it on.
        DWORD WINAPI hangEntry(LPVOID)
        {
            reportHang();
            return 0;
        }

        /// **The terminate hook and the stack guarantee on every thread**, because MSVC's runtime
        /// keeps the hook per thread and a new one starts with the default, which aborts, and the
        /// guarantee is per thread too. The loader calls this in each thread it starts, before the
        /// thread's own function; a thread started before `install` keeps the runtime's.
        void NTAPI onThreadStart(PVOID, DWORD reason, PVOID)
        {
            if (reason == DLL_THREAD_ATTACH && isInstalled())
            {
                std::set_terminate(onTerminate);
                guaranteeStack();
            }
        }
    }

    std::filesystem::path executable()
    {
        std::wstring path(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length < path.size())
            {
                path.resize(length);
                return path;
            }
            path.resize(path.size() * 2);
        }
    }

    void prepareInstallingThread()
    {
        guaranteeStack();
    }

    void hookEveryEnd(Heartbeat& page)
    {
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        std::signal(SIGABRT, onAbort);
        _set_purecall_handler(onPureCall);
        _set_invalid_parameter_handler(onInvalidParameter);
        std::atomic_ref(page.mHangEntry).store(reinterpret_cast<std::uint64_t>(&hangEntry), std::memory_order_release);
    }

    void endAsCrash(std::string_view reason)
    {
        reportAndEnd(reason);
    }
}

// The loader calls every pointer in `.CRT$XL*` at each thread's start. The two names keep the
// linker from dropping the table and the entry, which nothing else refers to; a 32-bit build
// spells a C name with a leading underscore.
#if defined(_M_IX86)
#pragma comment(linker, "/INCLUDE:__tls_used")
#pragma comment(linker, "/INCLUDE:_openmwCrashThreadStart")
#else
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:openmwCrashThreadStart")
#endif
#pragma section(".CRT$XLY", long, read)
extern "C" __declspec(
    allocate(".CRT$XLY")) const PIMAGE_TLS_CALLBACK openmwCrashThreadStart = Crash::Client::onThreadStart;
