#include "crashpadclientsystem.hpp"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <client/crashpad_client.h>
#include <util/misc/capture_context.h>
#include <util/win/context_wrappers.h>

#include <components/misc/windows.hpp>

#include "crashnote.hpp"
#include "crashsummary.hpp"

namespace Crash::Client
{
    namespace
    {
        /// A crash reported the way the system's own would not be, which ends the process.
        ///
        /// **Through Crashpad's unhandled-exception path and not its simulated crash**, which waits
        /// for the dump with no limit: where the monitor had died before, the game hung for good
        /// rather than crashing. This one waits sixty seconds and ends the process either way. Under
        /// `sSimulatedException`, so the monitor reads the dump as one the game asked for, and the
        /// notes say why.
        [[noreturn]] void reportAndEnd(std::string_view reason)
        {
            finalReport(ReportKind::Crash, reason);

            CONTEXT context;
            crashpad::CaptureContext(&context);
            EXCEPTION_RECORD record{};
            record.ExceptionCode = sSimulatedException;
            record.ExceptionAddress = crashpad::ProgramCounterFromCONTEXT(&context);
            EXCEPTION_POINTERS pointers{ .ExceptionRecord = &record, .ContextRecord = &context };
            crashpad::CrashpadClient::DumpAndCrash(&pointers);
            std::_Exit(3);
        }

        /// **A fault waits out a report another thread is writing**, as the POSIX half's does, and for
        /// a worse reason here: the fault's dump ends the process, and took the report another
        /// thread had begun with it, which then left no dump at all. Bounded at two seconds, for
        /// the reason the POSIX half gives. Then Crashpad's own filter takes the fault.
        bool onFault(EXCEPTION_POINTERS*)
        {
            for (int waited = 0; takeForFault() == FaultGate::Other && waited < 2000; ++waited)
                Sleep(1);
            return false;
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
        /// run in three. What `SetThreadStackGuarantee` reserves is there for exactly that: 64 KiB,
        /// far more than the dispatch and a filter that signals its handler and waits take, and a
        /// sixteenth of the megabyte a thread's stack reserves by default.
        void guaranteeStack()
        {
            ULONG guarantee = 64 * 1024;
            SetThreadStackGuarantee(&guarantee);
        }

        /// The file name of Crashpad's WER module, which `link.cmake` gives it and puts beside the
        /// executables.
        constexpr std::string_view sWerModule = OPENMW_WER_MODULE;

        /// Where WER reads the modules it may load for a process (`WerRegisterRuntimeExceptionModule`):
        /// a value a module, named by its whole path, whose contents WER ignores ("WER Settings").
        constexpr const wchar_t* sWerModules
            = L"Software\\Microsoft\\Windows\\Windows Error Reporting\\RuntimeExceptionHelperModules";

        /// Whether `value` names a module of `wer`'s name that is no longer on disk: a copy of the game
        /// that moved or went. A copy that stands keeps its value, since its game may be running.
        bool namesAMovedCopy(const std::wstring& value, const std::filesystem::path& wer)
        {
            const std::filesystem::path named(value);
            const std::wstring ours = wer.filename().wstring();
            const std::wstring theirs = named.filename().wstring();
            if (CompareStringOrdinal(
                    theirs.c_str(), static_cast<int>(theirs.size()), ours.c_str(), static_cast<int>(ours.size()), TRUE)
                != CSTR_EQUAL)
                return false;

            std::error_code unread;
            return !std::filesystem::exists(named, unread) && !unread;
        }

        /// Removes the values of `key` that name a moved copy of the module `wer`, so the key holds a value
        /// for each copy that stands and none for one that went.
        void forgetMovedCopies(HKEY key, const std::filesystem::path& wer)
        {
            std::vector<std::wstring> moved;
            // The longest value name the registry takes, and its terminator.
            std::wstring name(16384, L'\0');
            for (DWORD index = 0;; ++index)
            {
                DWORD length = static_cast<DWORD>(name.size());
                const LSTATUS read
                    = RegEnumValueW(key, index, name.data(), &length, nullptr, nullptr, nullptr, nullptr);
                if (read != ERROR_SUCCESS)
                    break;
                const std::wstring value(name.data(), length);
                if (namesAMovedCopy(value, wer))
                    moved.push_back(value);
            }
            // Apart from the walk, which an index into a key whose values are being removed would
            // skip through.
            for (const std::wstring& value : moved)
                RegDeleteValueW(key, value.c_str());
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
        /// thread's own function; a thread started before `install` gets neither.
        void NTAPI onThreadStart(PVOID, DWORD reason, PVOID)
        {
            if (reason == DLL_THREAD_ATTACH && isInstalled())
            {
                std::set_terminate(onTerminate);
                guaranteeStack();
            }
        }
    }

    void keepConnectionToThisProcess() {}

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
        crashpad::CrashpadClient::SetFirstChanceExceptionHandler(onFault);
    }

    std::string_view catchPastTheProcess(crashpad::CrashpadClient& client, const std::filesystem::path& executable)
    {
        const std::filesystem::path wer = executable.parent_path() / sWerModule;
        std::error_code unread;
        if (!std::filesystem::is_regular_file(wer, unread))
            return "a fail-fast's dump: the WER module is not beside the executable";

        // The player's key, which needs no administrator: the package is a folder and has no
        // installer to write the machine's.
        HKEY key = nullptr;
        if (RegCreateKeyExW(
                HKEY_CURRENT_USER, sWerModules, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr)
            != ERROR_SUCCESS)
            return "a fail-fast's dump: WER's list of modules could not be opened";

        forgetMovedCopies(key, wer);
        const DWORD ignored = 1;
        const LSTATUS listed
            = RegSetValueExW(key, wer.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&ignored), sizeof(ignored));
        RegCloseKey(key);
        if (listed != ERROR_SUCCESS)
            return "a fail-fast's dump: the WER module could not be listed";

        if (!client.RegisterWerModule(wer.wstring()))
            return "a fail-fast's dump: WER refused the module";

        // **WER calls no module for a process whose error mode asks for no fault box**, and a
        // process inherits its parent's mode: started from Git Bash, the game lost every
        // fail-fast's dump. The box shows only where the module could not reach the monitor, as it
        // does for any program, since every other crash is the catcher's own.
        SetErrorMode(GetErrorMode() & ~SEM_NOGPFAULTERRORBOX);
        return {};
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
