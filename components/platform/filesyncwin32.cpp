#include "filesync.hpp"

#include <components/misc/windows.hpp>

namespace Platform
{
    bool syncFile(const std::filesystem::path& path)
    {
        // Opened for writing, because `FlushFileBuffers` asks for a handle with write access.
        const HANDLE file
            = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        const bool synced = FlushFileBuffers(file) != 0;
        CloseHandle(file);
        return synced;
    }
}
