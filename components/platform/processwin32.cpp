#include "process.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include <components/misc/windows.hpp>

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
