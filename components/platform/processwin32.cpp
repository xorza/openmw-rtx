#include "process.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/misc/windows.hpp>

// After the tree's Windows header, whose declarations the SDK's headers below assume.
#include <psapi.h>
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

    std::vector<std::string> commandLine(std::int32_t, char**)
    {
        std::vector<std::string> arguments;
        std::int32_t count = 0;
        wchar_t** const wide = CommandLineToArgvW(GetCommandLineW(), &count);
        for (std::int32_t i = 0; i < count; ++i)
        {
            const std::int32_t size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string one(static_cast<std::size_t>(std::max(size, 1)) - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, one.data(), size, nullptr, nullptr);
            arguments.push_back(std::move(one));
        }
        LocalFree(static_cast<HLOCAL>(wide));
        return arguments;
    }

    namespace
    {
        bool isRedirected(DWORD nStdHandle)
        {
            const DWORD fileType = GetFileType(GetStdHandle(nStdHandle));
            return fileType == FILE_TYPE_DISK || fileType == FILE_TYPE_PIPE;
        }
    }

    void attachParentConsole()
    {
        if (GetConsoleWindow() == nullptr)
        {
            const bool inRedirected = isRedirected(STD_INPUT_HANDLE);
            const bool outRedirected = isRedirected(STD_OUTPUT_HANDLE);
            const bool errRedirected = isRedirected(STD_ERROR_HANDLE);

            // Note: Do not spend three days reinvestigating this PowerShell bug thinking its our bug.
            // https://gitlab.com/OpenMW/openmw/-/merge_requests/408#note_447467393
            // The handles look valid, but GetFinalPathNameByHandleA can't tell what files they go to and writing to
            // them doesn't work.
            if (AttachConsole(ATTACH_PARENT_PROCESS))
            {
                static_cast<void>(fflush(stdout));
                static_cast<void>(fflush(stderr));
                std::cout.flush();
                std::cerr.flush();

                // this looks dubious but is really the right way
                if (!inRedirected)
                {
                    static_cast<void>(_wfreopen(L"CON", L"r", stdin));
                    static_cast<void>(freopen("CON", "r", stdin));
                    std::cin.clear();
                }
                if (!outRedirected)
                {
                    static_cast<void>(_wfreopen(L"CON", L"w", stdout));
                    static_cast<void>(freopen("CON", "w", stdout));
                    std::cout.clear();
                }
                if (!errRedirected)
                {
                    static_cast<void>(_wfreopen(L"CON", L"w", stderr));
                    static_cast<void>(freopen("CON", "w", stderr));
                    std::cerr.clear();
                }
            }
        }
        SetConsoleOutputCP(CP_UTF8);
    }

    bool standardErrorTakesColour()
    {
        DWORD mode = 0;
        if (GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode) && (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0)
            return true;

        // some console emulators may not use the Win32 API, so try the Unixy approach
        return std::getenv("TERM") != nullptr && GetFileType(GetStdHandle(STD_ERROR_HANDLE)) == FILE_TYPE_CHAR;
    }

    std::string_view executableSuffix()
    {
        return ".exe";
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

    bool showsADesktop()
    {
        USEROBJECTFLAGS flags{};
        DWORD needed = 0;
        // Where the station cannot be asked, a presenting run goes on and says what failed.
        if (!GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS, &flags, sizeof(flags), &needed))
            return true;
        return (flags.dwFlags & WSF_VISIBLE) != 0;
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
        const std::int32_t length
            = MultiByteToWideChar(CP_UTF8, 0, quoted.data(), static_cast<std::int32_t>(quoted.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, quoted.data(), static_cast<std::int32_t>(quoted.size()), wide.data(), length);
        return CommandEnd{ .mExitCode = static_cast<std::uint32_t>(_wsystem(wide.c_str())) };
    }

    void restartOnHugePages(char** /*argv*/) {}

    bool refuseTransparentHugePages()
    {
        return true;
    }

    std::optional<float> hugePageShare()
    {
        return std::nullopt;
    }

    std::optional<std::uint64_t> compactionStalls()
    {
        return std::nullopt;
    }

    std::optional<AnonymousMemory> anonymousMemory()
    {
        PROCESS_MEMORY_COUNTERS_EX2 counters{};
        counters.cb = sizeof(counters);
        // The `EX2` layout is the call's own, asked for by its size, which the call reads through the base type.
        if (!GetProcessMemoryInfo(
                GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
            return std::nullopt;
        const std::uint64_t resident = counters.PrivateWorkingSetSize;
        const std::uint64_t committed = counters.PrivateUsage;
        return AnonymousMemory{ .mResident = resident, .mSwapped = committed - std::min(resident, committed) };
    }

    std::optional<PrivateMemoryHeld> privateMemoryHeld()
    {
        // The heaps' own allocations, by the base `VirtualQuery` names each under: a heap's regions, and each block
        // a heap gave memory of its own, which `HeapWalk` lists busy and past a region. Every heap is locked while it
        // is walked, as `HeapWalk` asks.
        std::vector<HANDLE> heaps(GetProcessHeaps(0, nullptr));
        heaps.resize(GetProcessHeaps(static_cast<DWORD>(heaps.size()), heaps.data()));
        std::vector<const void*> heapBases;
        const auto baseOf = [](const void* address) -> const void* {
            MEMORY_BASIC_INFORMATION region{};
            return VirtualQuery(address, &region, sizeof(region)) == sizeof(region) ? region.AllocationBase : nullptr;
        };
        for (const HANDLE heap : heaps)
        {
            if (!HeapLock(heap))
                continue;
            PROCESS_HEAP_ENTRY entry{};
            while (HeapWalk(heap, &entry))
                if ((entry.wFlags & PROCESS_HEAP_REGION) != 0 || (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0)
                    if (const void* const base = baseOf(entry.lpData); base != nullptr)
                        heapBases.push_back(base);
            HeapUnlock(heap);
        }
        std::ranges::sort(heapBases);
        heapBases.erase(std::ranges::unique(heapBases).begin(), heapBases.end());

        // Every committed private region of the address space, a region at a time.
        PrivateMemoryHeld held;
        const char* address = nullptr;
        MEMORY_BASIC_INFORMATION region{};
        while (VirtualQuery(address, &region, sizeof(region)) == sizeof(region))
        {
            if (region.State == MEM_COMMIT && region.Type == MEM_PRIVATE)
            {
                const std::uint64_t bytes = region.RegionSize;
                if (std::ranges::binary_search(heapBases, static_cast<const void*>(region.AllocationBase)))
                    held.mHeaps += bytes;
                else
                {
                    held.mOther += bytes;
                    ++held.mOtherRegions;
                    held.mLargestOther = std::max(held.mLargestOther, bytes);
                }
            }
            address = static_cast<const char*>(region.BaseAddress) + region.RegionSize;
        }
        return held;
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

    std::size_t usableCpuCount()
    {
        // The process's default CPU sets where it has any, which `keepToPerformanceCores` sets, and
        // every active processor of every group where it has none.
        ULONG count = 0;
        GetProcessDefaultCpuSets(GetCurrentProcess(), nullptr, 0, &count);
        if (count > 0)
            return count;
        return std::max<std::size_t>(1, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    }
}
