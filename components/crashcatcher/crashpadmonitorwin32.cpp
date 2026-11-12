#include "crashpadmonitorsystem.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <handler/handler_main.h>
#include <snapshot/exception_snapshot.h>
#include <snapshot/win/process_snapshot_win.h>
#include <util/win/scoped_process_suspend.h>
#include <util/win/termination_codes.h>

#include <components/misc/windows.hpp>

#include "crashsummary.hpp"

namespace Crash::Monitor
{
    namespace
    {
        HANDLE handleOf(std::intptr_t hold)
        {
            return hold < 0 ? nullptr : reinterpret_cast<HANDLE>(hold);
        }

        /// What an exception code, or the exit code a fail-fast leaves, is called.
        constexpr std::array<std::pair<std::uint32_t, std::string_view>, 13> sExceptionNames{ {
            { EXCEPTION_ACCESS_VIOLATION, "EXCEPTION_ACCESS_VIOLATION" },
            { EXCEPTION_IN_PAGE_ERROR, "EXCEPTION_IN_PAGE_ERROR" },
            { EXCEPTION_STACK_OVERFLOW, "EXCEPTION_STACK_OVERFLOW" },
            { EXCEPTION_ILLEGAL_INSTRUCTION, "EXCEPTION_ILLEGAL_INSTRUCTION" },
            { EXCEPTION_PRIV_INSTRUCTION, "EXCEPTION_PRIV_INSTRUCTION" },
            { EXCEPTION_INT_DIVIDE_BY_ZERO, "EXCEPTION_INT_DIVIDE_BY_ZERO" },
            { EXCEPTION_INT_OVERFLOW, "EXCEPTION_INT_OVERFLOW" },
            { EXCEPTION_DATATYPE_MISALIGNMENT, "EXCEPTION_DATATYPE_MISALIGNMENT" },
            { EXCEPTION_BREAKPOINT, "EXCEPTION_BREAKPOINT" },
            { EXCEPTION_NONCONTINUABLE_EXCEPTION, "EXCEPTION_NONCONTINUABLE_EXCEPTION" },
            { 0xC0000374, "STATUS_HEAP_CORRUPTION" },
            { 0xC0000409, "STATUS_STACK_BUFFER_OVERRUN" },
            { 0xC0000602, "STATUS_FAIL_FAST_EXCEPTION" },
        } };
    }

    GameProcess::GameProcess(std::uint32_t id)
        : mId(id)
    {
        // What `ProcessSnapshotWin` reads a hang's dump with besides: the memory, and a suspension.
        const HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_SUSPEND_RESUME
                | PROCESS_DUP_HANDLE | PROCESS_TERMINATE | SYNCHRONIZE,
            FALSE, static_cast<DWORD>(id));
        if (handle != nullptr)
            mHold = Platform::UniqueHold<Closing>(reinterpret_cast<std::intptr_t>(handle));
    }

    void GameProcess::Closing::close(const Handle hold) noexcept
    {
        CloseHandle(reinterpret_cast<HANDLE>(hold));
    }

    Ending GameProcess::end() const
    {
        const HANDLE handle = handleOf(mHold.get());
        if (handle == nullptr)
            return Ending::Failed;
        if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0)
            return Ending::Gone;
        return TerminateProcess(handle, 3) != FALSE ? Ending::Ended : Ending::Failed;
    }

    bool GameProcess::isHeld() const
    {
        BOOL debugged = FALSE;
        const HANDLE handle = handleOf(mHold.get());
        return handle != nullptr && CheckRemoteDebuggerPresent(handle, &debugged) != FALSE && debugged != FALSE;
    }

    std::optional<std::uint32_t> GameProcess::exitCode() const
    {
        DWORD code = 0;
        const HANDLE handle = handleOf(mHold.get());
        if (handle == nullptr || GetExitCodeProcess(handle, &code) == FALSE || code == STILL_ACTIVE)
            return std::nullopt;
        return static_cast<std::uint32_t>(code);
    }

    bool GameProcess::isGone() const
    {
        const HANDLE handle = handleOf(mHold.get());
        return handle == nullptr || WaitForSingleObject(handle, 0) != WAIT_TIMEOUT;
    }

    bool GameProcess::readWhole(
        const SnapshotStamp& stamp, const std::function<bool(const crashpad::ProcessSnapshot&)>& use) const
    {
        const HANDLE handle = handleOf(mHold.get());
        if (handle == nullptr)
            return false;

        // Suspensions count, so a crash's dump that suspends the game meanwhile nests with this one.
        const crashpad::ScopedProcessSuspend suspended(handle);
        crashpad::ProcessSnapshotWin snapshot;
        if (!snapshot.Initialize(handle, crashpad::ProcessSuspensionState::kSuspended, 0, 0))
            return false;
        snapshot.SetReportID(stamp.mReport);
        snapshot.SetClientID(stamp.mClient);
        snapshot.SetAnnotationsSimpleMap(stamp.mAnnotations);
        return use(snapshot);
    }

    int runHandler(std::vector<std::string>& arguments, const HandlerParts& parts)
    {
        std::vector<char*> argv;
        for (std::string& argument : arguments)
            argv.push_back(argument.data());
        argv.push_back(nullptr);
        return crashpad::HandlerMain(static_cast<int>(argv.size() - 1), argv.data(), &parts.mSources);
    }

    std::string describeExitCode(std::uint32_t code)
    {
        return nameOf(sExceptionNames, code, hex(code));
    }

    bool isCrashCode(std::uint32_t code)
    {
        return code == crashpad::kTerminationCodeCrashNoDump
            || std::ranges::any_of(sExceptionNames, [&](const auto& named) { return named.first == code; });
    }

    std::string describeException(const crashpad::ExceptionSnapshot& exception, std::uint32_t)
    {
        const std::uint32_t code = exception.Exception();
        if (code == sSimulatedException)
            return {};

        std::string text = nameOf(sExceptionNames, code, "exception " + hex(code));

        const std::vector<std::uint64_t>& codes = exception.Codes();
        if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && codes.size() >= 2)
            text += std::string(codes[0] == 0 ? " reading "
                            : codes[0] == 1   ? " writing "
                                              : " executing ")
                + hex(codes[1]);
        return text;
    }
}
