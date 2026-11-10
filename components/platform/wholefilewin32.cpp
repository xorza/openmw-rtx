#include "wholefilesystem.hpp"

#include <algorithm>
#include <cstdint>

#include <components/misc/windows.hpp>

#include "uniquehold.hpp"

namespace Platform::WholeFileSystem
{
    namespace
    {
        struct FileHandle
        {
            using Handle = HANDLE;
            static inline const Handle sNone = INVALID_HANDLE_VALUE;
            static void close(Handle handle) noexcept { CloseHandle(handle); }
        };

        IoError errorOf(const DWORD code)
        {
            const auto failure = [code] {
                switch (code)
                {
                    case ERROR_FILE_NOT_FOUND:
                    case ERROR_PATH_NOT_FOUND:
                    case ERROR_INVALID_NAME:
                        return IoFailure::NotFound;
                    case ERROR_ACCESS_DENIED:
                    case ERROR_SHARING_VIOLATION:
                    case ERROR_LOCK_VIOLATION:
                    case ERROR_WRITE_PROTECT:
                        return IoFailure::Denied;
                    case ERROR_DISK_FULL:
                    case ERROR_HANDLE_DISK_FULL:
                    case ERROR_DISK_QUOTA_EXCEEDED:
                        return IoFailure::NoSpace;
                    default:
                        return IoFailure::Other;
                }
            }();
            return IoError{ .mFailure = failure,
                .mCode = std::error_code(static_cast<int>(code), std::system_category()) };
        }

        Err<IoError> lastError()
        {
            return Err<IoError>{ errorOf(GetLastError()) };
        }

        /// The most one `ReadFile` or `WriteFile` is asked for: their count is a `DWORD`.
        constexpr std::size_t sMostAtOnce = std::size_t{ 1 } << 30;

        /// How often a rename refused because another process holds the target is tried, and how
        /// long apart: an indexer or a virus scanner opens a file it saw change and lets it go
        /// within milliseconds, as PostgreSQL's `pgrename` and Python's `atomicwrites` find.
        constexpr int sReplaceTries = 10;
        constexpr DWORD sReplaceWaitMs = 20;
    }

    Result<void, IoError> read(const std::filesystem::path& path, const Sink sink, const std::size_t most)
    {
        const UniqueHold<FileHandle> file(
            CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file.isOpen())
            return lastError();

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.get(), &size))
            return lastError();

        const auto stated = static_cast<std::size_t>(std::max<LONGLONG>(size.QuadPart, 0));
        if (stated > most)
            return tooLarge();

        // Sized to what the system states and one more, so a file that ends where it said is read to
        // its end by one read past it; grown by half where it runs on.
        std::size_t capacity = stated + 1;
        std::size_t filled = 0;
        std::byte* data = sink.mGrow(sink.mBuffer, capacity);
        while (true)
        {
            if (filled == capacity)
            {
                capacity += capacity / 2;
                data = sink.mGrow(sink.mBuffer, capacity);
            }

            DWORD got = 0;
            const auto asked = static_cast<DWORD>(std::min(capacity - filled, sMostAtOnce));
            if (!ReadFile(file.get(), data + filled, asked, &got, nullptr))
                return lastError();
            if (got == 0)
                break;
            filled += got;
            if (filled > most)
                return tooLarge();
        }

        sink.mGrow(sink.mBuffer, filled);
        return {};
    }

    Result<void, IoError> create(const std::filesystem::path& path,
        const std::span<const std::span<const std::byte>> parts, const bool synced, const std::filesystem::path&)
    {
        // A replaced file's permissions are its folder's on Windows, which the rename keeps.
        const HANDLE handle
            = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return lastError();

        const auto writeAll = [&]() -> Result<void, IoError> {
            for (std::span<const std::byte> part : parts)
                while (!part.empty())
                {
                    DWORD written = 0;
                    const auto asked = static_cast<DWORD>(std::min(part.size(), sMostAtOnce));
                    if (!WriteFile(handle, part.data(), asked, &written, nullptr))
                        return lastError();
                    part = part.subspan(written);
                }
            if (synced && !FlushFileBuffers(handle))
                return lastError();
            return {};
        };

        Result<void, IoError> written = writeAll();
        if (!CloseHandle(handle) && written.isOk())
            written = lastError();
        return written;
    }

    Result<void, IoError> replace(const std::filesystem::path& from, const std::filesystem::path& to, const bool synced)
    {
        // **`MOVEFILE_WRITE_THROUGH` for a synced write**: it returns once the rename is on the disk,
        // which is what a folder's sync gives on POSIX, and Windows has no folder sync.
        const DWORD flags = MOVEFILE_REPLACE_EXISTING | (synced ? MOVEFILE_WRITE_THROUGH : 0);
        for (int tried = 1;; ++tried)
        {
            if (MoveFileExW(from.c_str(), to.c_str(), flags))
                return {};

            const DWORD code = GetLastError();
            const bool held = code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION;
            if (!held || tried == sReplaceTries)
                return Err<IoError>{ errorOf(code) };
            Sleep(sReplaceWaitMs);
        }
    }

    void remove(const std::filesystem::path& path) noexcept
    {
        DeleteFileW(path.c_str());
    }
}
