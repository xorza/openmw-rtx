#include "appendfile.hpp"

#include <algorithm>

#include <components/misc/windows.hpp>

namespace Platform
{
    AppendFile AppendFile::open(const std::filesystem::path& path, const bool emptied)
    {
        AppendFile file;
        // Shared with every other writer and reader: the game and its monitor hold it at once, and
        // a player reads it while the game runs.
        const HANDLE handle = CreateFileW(path.c_str(), FILE_APPEND_DATA | (emptied ? GENERIC_WRITE : 0),
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, emptied ? CREATE_ALWAYS : OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return file;

        // `CREATE_ALWAYS` needed the right to write anywhere to empty it; reopened with the right to
        // append alone, which is what makes every write land at the end.
        if (emptied)
        {
            CloseHandle(handle);
            return open(path, false);
        }

        file.mHold = reinterpret_cast<std::intptr_t>(handle);
        return file;
    }

    AppendFile::~AppendFile()
    {
        if (isOpen())
            CloseHandle(reinterpret_cast<HANDLE>(mHold));
    }

    void AppendFile::write(std::string_view bytes) const
    {
        while (isOpen() && !bytes.empty())
        {
            const DWORD asked = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1u << 30));
            DWORD written = 0;
            if (!WriteFile(reinterpret_cast<HANDLE>(mHold), bytes.data(), asked, &written, nullptr) || written == 0)
                return;
            bytes.remove_prefix(written);
        }
    }
}
