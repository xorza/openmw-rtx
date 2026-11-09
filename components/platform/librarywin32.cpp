#include "library.hpp"

#include <components/misc/windows.hpp>

namespace Platform::Library
{
    Handle open(const char* name)
    {
        // The system's own directories and nothing relative to the process, which is where a
        // driver's library is and where a stray copy beside the binary is not.
        return static_cast<Handle>(
            reinterpret_cast<std::intptr_t>(LoadLibraryExA(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)));
    }

    Handle open(const std::filesystem::path& path)
    {
        // The DriverStore copy is named by its full path; its directory is where the driver's other
        // libraries are, so the altered search path finds them and leaves the process's own first.
        return static_cast<Handle>(
            reinterpret_cast<std::intptr_t>(LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)));
    }

    void* find(const Handle handle, const char* symbol)
    {
        return reinterpret_cast<void*>(
            GetProcAddress(reinterpret_cast<HMODULE>(static_cast<std::intptr_t>(handle)), symbol));
    }

    void close(const Handle handle)
    {
        FreeLibrary(reinterpret_cast<HMODULE>(static_cast<std::intptr_t>(handle)));
    }
}
