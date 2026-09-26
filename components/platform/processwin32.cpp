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
