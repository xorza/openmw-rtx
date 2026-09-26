#include "crashpadmonitorsystem.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include <snapshot/exception_snapshot.h>

#include <components/misc/windows.hpp>

#include <shellapi.h>

namespace Crash::Monitor
{
    namespace
    {
        HANDLE handleOf(std::intptr_t hold)
        {
            return hold < 0 ? nullptr : reinterpret_cast<HANDLE>(hold);
        }
    }

    GameProcess::GameProcess(std::uint32_t id)
        : mId(id)
    {
        const HANDLE handle = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION
                | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_TERMINATE | SYNCHRONIZE,
            FALSE, static_cast<DWORD>(id));
        if (handle != nullptr)
            mHold = reinterpret_cast<std::intptr_t>(handle);
    }

    GameProcess::~GameProcess()
    {
        if (const HANDLE handle = handleOf(mHold))
            CloseHandle(handle);
    }

    void GameProcess::requestHangReport(Heartbeat& page) const
    {
        const HANDLE handle = handleOf(mHold);
        const auto entry = std::atomic_ref(page.mHangEntry).load();
        if (handle == nullptr || entry == 0)
            return;

        if (const HANDLE thread = CreateRemoteThread(
                handle, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(entry), nullptr, 0, nullptr))
            CloseHandle(thread);
    }

    bool GameProcess::end() const
    {
        const HANDLE handle = handleOf(mHold);
        if (handle == nullptr || WaitForSingleObject(handle, 0) == WAIT_OBJECT_0)
            return false;
        return TerminateProcess(handle, 3) != FALSE;
    }

    std::string describeException(const crashpad::ExceptionSnapshot& exception, std::uint32_t)
    {
        const std::uint32_t code = exception.Exception();
        if (code == 0x517a7ed)
            return {};

        static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 12> sNames{ {
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
        } };
        std::string text = nameOf(sNames, code, "exception " + hex(code));

        const std::vector<std::uint64_t>& codes = exception.Codes();
        if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && codes.size() >= 2)
            text += std::string(codes[0] == 0 ? " reading "
                            : codes[0] == 1   ? " writing "
                                              : " executing ")
                + hex(codes[1]);
        return text;
    }

    std::vector<std::string> commandLine(int, char**)
    {
        std::vector<std::string> arguments;
        int count = 0;
        wchar_t** const wide = CommandLineToArgvW(GetCommandLineW(), &count);
        for (int i = 0; i < count; ++i)
        {
            const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string one(static_cast<std::size_t>(std::max(size, 1)) - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, one.data(), size, nullptr, nullptr);
            arguments.push_back(std::move(one));
        }
        LocalFree(wide);
        return arguments;
    }

    std::tm localTime(std::time_t seconds)
    {
        std::tm local{};
        localtime_s(&local, &seconds);
        return local;
    }

    std::string_view dumpFolder()
    {
        return "reports";
    }
}
