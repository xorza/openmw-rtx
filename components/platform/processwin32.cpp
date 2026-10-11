#include "process.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
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

    namespace
    {
        /// A variable's name, which is ASCII, in the wide spelling the wide calls take.
        std::wstring wideName(const char* name)
        {
            return std::wstring(name, name + std::strlen(name));
        }
    }

    void setEnvironmentPath(const char* name, const std::filesystem::path& value)
    {
        _wputenv_s(wideName(name).c_str(), value.c_str());
    }

    std::optional<std::filesystem::path> environmentPath(const char* name)
    {
        const wchar_t* const value = _wgetenv(wideName(name).c_str());
        if (value == nullptr)
            return std::nullopt;
        return std::filesystem::path(value);
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
        // `cmd` expands `%NAME%` inside double quotes, and a quote inside ends the word: neither has
        // a spelling that survives `std::system`, so such a word is refused rather than run as
        // another. A lone `%` is no name and stays, which is what a frame pattern's `%05d` is.
        if (text.find('"') != std::string_view::npos || std::ranges::count(text, '%') > 1)
            throw std::invalid_argument(
                "\"" + std::string(text) + "\" holds a \" or two %, which cmd cannot take as one word");

        // **A trailing run of backslashes doubled**, as a program that parses its line by the
        // C runtime's rules reads one back: a backslash before the closing quote escaped it, and
        // `C:\frames\` ran into the next word.
        const std::size_t trailing = text.size() - std::min(text.find_last_not_of('\\') + 1, text.size());
        return '"' + std::string(text) + std::string(trailing, '\\') + '"';
    }

    CommandEnd runShell(const std::string& line)
    {
        // **Wide**, because the line is UTF-8 and `std::system` reads the process's ANSI code page:
        // a path through `C:\Users\Jörg` reached `cmd` as another path.
        const std::string quoted = '"' + line + '"';
        const int length = MultiByteToWideChar(CP_UTF8, 0, quoted.data(), static_cast<int>(quoted.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, quoted.data(), static_cast<int>(quoted.size()), wide.data(), length);
        return CommandEnd{ .mExitCode = static_cast<std::uint32_t>(_wsystem(wide.c_str())) };
    }

    void restartOnHugePages(char** /*argv*/) {}

    std::optional<float> hugePageShare()
    {
        return std::nullopt;
    }

    std::size_t keepToPerformanceCores()
    {
        const HANDLE process = GetCurrentProcess();
        ULONG length = 0;
        GetSystemCpuSetInformation(nullptr, 0, &length, process, 0);
        std::vector<std::byte> buffer(length);
        if (length == 0
            || !GetSystemCpuSetInformation(
                reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()), length, &length, process, 0))
            return 0;

        // The entries are of their own sizes, each saying its own. The highest efficiency class is
        // the fastest core, and a system with one class has no choice to make.
        const auto forEachSet = [&](auto&& visit) {
            for (std::size_t at = 0; at < length;)
            {
                const auto* const entry = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + at);
                if (entry->Size == 0)
                    return;
                if (entry->Type == CpuSetInformation)
                    visit(entry->CpuSet);
                at += entry->Size;
            }
        };
        BYTE lowest = 255;
        BYTE highest = 0;
        forEachSet([&](const auto& set) {
            lowest = std::min(lowest, set.EfficiencyClass);
            highest = std::max(highest, set.EfficiencyClass);
        });
        if (lowest >= highest)
            return 0;

        std::vector<ULONG> fastest;
        forEachSet([&](const auto& set) {
            if (set.EfficiencyClass == highest)
                fastest.push_back(set.Id);
        });

        // The process's default, which every thread without a set of its own runs on, those started
        // before this call among them.
        if (!SetProcessDefaultCpuSets(process, fastest.data(), static_cast<ULONG>(fastest.size())))
            return 0;

        return fastest.size();
    }
}
