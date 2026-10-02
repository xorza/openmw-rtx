#include "process.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/misc/windows.hpp>

#include <shellapi.h>

namespace Platform::Process
{
    bool setEnvironmentDefault(const char* name, const char* value)
    {
        if (std::getenv(name) != nullptr)
            return false;

        _putenv_s(name, value);
        return true;
    }

    void setEnvironment(const char* name, const char* value)
    {
        _putenv_s(name, value);
    }

    std::optional<std::filesystem::path> executable()
    {
        std::wstring path(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0)
                return std::nullopt;
            if (length < path.size())
            {
                path.resize(length);
                return std::filesystem::path(path);
            }
            path.resize(path.size() * 2);
        }
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

    std::uint32_t currentId()
    {
        return static_cast<std::uint32_t>(GetCurrentProcessId());
    }

    bool isRunning(const std::uint32_t id)
    {
        if (id == 0)
            return false;

        // A process that ended keeps its object while anything holds a handle to it, so being
        // openable is not running: what says so is that it has not yet signalled its end.
        const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(id));
        if (process == nullptr)
            return GetLastError() == ERROR_ACCESS_DENIED;

        const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
        CloseHandle(process);
        return running;
    }

    std::uint64_t currentThreadId()
    {
        return GetCurrentThreadId();
    }

    bool startedFromTerminal()
    {
        return false;
    }

    std::string shellWord(std::string_view text)
    {
        return '"' + std::string(text) + '"';
    }

    CommandEnd runShell(const std::string& line)
    {
        return CommandEnd{ .mExitCode = static_cast<std::uint32_t>(std::system(('"' + line + '"').c_str())) };
    }

    void disableCoreDump() {}
}
