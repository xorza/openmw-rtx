#include "crashpadclientsystem.hpp"

#include <array>
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
        /// rather than crashing. This one waits sixty seconds and ends the process either way, and
        /// is not taken at all where the monitor is known to have ended. Under
        /// `sSimulatedException`, so the monitor reads the dump as one the game asked for, and the
        /// notes say why.
        [[noreturn]] void reportAndEnd(std::string_view reason)
        {
            endingReason(reason);
            if (!monitorRuns())
            {
                sayUnreported(reason);
                std::_Exit(3);
            }

            CONTEXT context;
            crashpad::CaptureContext(&context);
            EXCEPTION_RECORD record{};
            record.ExceptionCode = sSimulatedException;
            record.ExceptionAddress = crashpad::ProgramCounterFromCONTEXT(&context);
            EXCEPTION_POINTERS pointers{ .ExceptionRecord = &record, .ContextRecord = &context };
            crashpad::CrashpadClient::DumpAndCrash(&pointers);
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

        /// The hooks `lostHooks` checks are still the catcher's, by name, in `SystemState::mHooks`'s
        /// order. Not the unhandled exception filter, which Windows reads only by setting it, and which
        /// would stand unset for that moment.
        constexpr std::array<std::string_view, 3> sHooked{
            "SIGABRT",
            "the pure virtual call handler",
            "the invalid parameter handler",
        };

        /// What each hook of `sHooked` is now.
        std::array<std::uintptr_t, 8> currentHooks()
        {
            return { reinterpret_cast<std::uintptr_t>(std::signal(SIGABRT, SIG_GET)),
                reinterpret_cast<std::uintptr_t>(_get_purecall_handler()),
                reinterpret_cast<std::uintptr_t>(_get_invalid_parameter_handler()) };
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

        /// The file name of Crashpad's WER module, which `CMakeLists.txt` gives it and puts beside the
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

        /// Whether `root` turns WER off by its `Disabled` value ("WER Settings"): Group Policy's, where
        /// it holds one, overrides the setting's own, as a policy does.
        bool disablesWer(HKEY root)
        {
            for (const wchar_t* key : { L"Software\\Policies\\Microsoft\\Windows\\Windows Error Reporting",
                     L"Software\\Microsoft\\Windows\\Windows Error Reporting" })
            {
                DWORD disabled = 0;
                DWORD size = sizeof(disabled);
                if (RegGetValueW(
                        root, key, L"Disabled", RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, nullptr, &disabled, &size)
                    == ERROR_SUCCESS)
                    return disabled != 0;
            }
            return false;
        }

        /// Whether WER's service, which a fail-fast is handed to, cannot start.
        bool werServiceDisabled()
        {
            const SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (manager == nullptr)
                return false;
            bool disabled = false;
            if (const SC_HANDLE service = OpenServiceW(manager, L"WerSvc", SERVICE_QUERY_CONFIG); service != nullptr)
            {
                DWORD needed = 0;
                QueryServiceConfigW(service, nullptr, 0, &needed);
                // In whole records, for the alignment of the strings the configuration carries after one.
                std::vector<QUERY_SERVICE_CONFIGW> config(
                    (needed + sizeof(QUERY_SERVICE_CONFIGW) - 1) / sizeof(QUERY_SERVICE_CONFIGW));
                disabled = !config.empty()
                    && QueryServiceConfigW(service, config.data(),
                        static_cast<DWORD>(config.size() * sizeof(QUERY_SERVICE_CONFIGW)), &needed)
                    && config.front().dwStartType == SERVICE_DISABLED;
                CloseServiceHandle(service);
            }
            CloseServiceHandle(manager);
            return disabled;
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

    void hookEveryEnd()
    {
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        std::signal(SIGABRT, onAbort);
        _set_purecall_handler(onPureCall);
        _set_invalid_parameter_handler(onInvalidParameter);
        systemState().mHooks = currentHooks();
    }

    void lostHooks(std::vector<std::string_view>& into)
    {
        const std::array<std::uintptr_t, 8> now = currentHooks();
        const std::array<std::uintptr_t, 8>& hooks = systemState().mHooks;
        for (std::size_t at = 0; at < sHooked.size(); ++at)
            if (now[at] != hooks[at])
                into.push_back(sHooked[at]);
    }

    std::string describeReading()
    {
        return "a suspended process";
    }

    std::string_view unreadable()
    {
        // A process reads every other of its user's.
        return {};
    }

    bool monitorRuns()
    {
        // Before the monitor has answered, `StartHandler` returned once it ran.
        const std::intptr_t held = systemState().mMonitor;
        return held < 0 || WaitForSingleObject(reinterpret_cast<HANDLE>(held), 0) == WAIT_TIMEOUT;
    }

    void watchMonitor(std::uint32_t process)
    {
        if (const HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(process)); handle != nullptr)
            systemState().mMonitor = reinterpret_cast<std::intptr_t>(handle);
    }

    std::string_view letMonitorRead(std::uint32_t)
    {
        // A process of the user's reads this one already.
        return {};
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

        // **Listed all the same where WER is off**, where the listing costs nothing: off, a fail-fast
        // ends the process with no module called, and the log says why its dump is missing.
        if (disablesWer(HKEY_LOCAL_MACHINE) || disablesWer(HKEY_CURRENT_USER))
            return "a fail-fast's dump: Windows Error Reporting's Disabled setting is on";
        if (werServiceDisabled())
            return "a fail-fast's dump: Windows Error Reporting's service is disabled";
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
extern "C" __declspec(allocate(".CRT$XLY")) const PIMAGE_TLS_CALLBACK openmwCrashThreadStart
    = Crash::Client::onThreadStart;
