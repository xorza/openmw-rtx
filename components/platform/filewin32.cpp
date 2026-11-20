#include "file.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <io.h>

#include <components/files/conversion.hpp>
#include <components/misc/windows.hpp>

namespace Platform::File
{
    namespace
    {
        auto getNativeHandle(Handle handle)
        {
            assert(handle != Handle::Invalid);

            return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(handle));
        }

        std::int32_t getNativeSeekType(SeekType seek)
        {
            if (seek == SeekType::Begin)
                return FILE_BEGIN;
            if (seek == SeekType::Current)
                return FILE_CURRENT;
            if (seek == SeekType::End)
                return FILE_END;
            return -1;
        }
    }

    Handle open(const std::filesystem::path& filename)
    {
        // Shared as a POSIX system shares it: an archive held open for its entries' reads is written by its own tool.
        HANDLE handle = CreateFileW(filename.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error(std::format(
                "Failed to open '{}' for reading: {}", Files::pathToUnicodeString(filename), GetLastError()));
        }
        return static_cast<Handle>(reinterpret_cast<std::intptr_t>(handle));
    }

    void close(Handle handle)
    {
        auto nativeHandle = getNativeHandle(handle);
        CloseHandle(nativeHandle);
    }

    void seek(Handle handle, std::size_t position, SeekType type /*= SeekType::Begin*/)
    {
        const auto nativeHandle = getNativeHandle(handle);
        const auto nativeSeekType = getNativeSeekType(type);

        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(position);
        if (!SetFilePointerEx(nativeHandle, li, nullptr, nativeSeekType))
            throw std::runtime_error(std::format("Failed to seek in file: {}", GetLastError()));
    }

    std::size_t size(Handle handle)
    {
        const auto nativeHandle = getNativeHandle(handle);
        LARGE_INTEGER li;
        if (!GetFileSizeEx(nativeHandle, &li))
            throw std::runtime_error(std::format("Failed to get file size: {}", GetLastError()));

        return static_cast<std::size_t>(li.QuadPart);
    }

    std::size_t tell(Handle handle)
    {
        const auto nativeHandle = getNativeHandle(handle);
        LARGE_INTEGER distance;
        distance.QuadPart = 0;
        LARGE_INTEGER li;
        if (!SetFilePointerEx(nativeHandle, distance, &li, FILE_CURRENT))
            throw std::runtime_error(std::format("Failed to get file offset: {}", GetLastError()));

        return static_cast<std::size_t>(li.QuadPart);
    }

    Result<std::size_t, std::error_code> readAt(
        Handle handle, std::uint64_t offset, void* data, std::size_t size) noexcept
    {
        const auto nativeHandle = getNativeHandle(handle);

        std::size_t done = 0;
        while (done < size)
        {
            // A handle opened without `FILE_FLAG_OVERLAPPED` reads at the structure's offset and returns once it has.
            // One call takes a `DWORD` of bytes at most, so a larger read is several.
            const std::uint64_t from = offset + done;
            OVERLAPPED at{};
            at.Offset = static_cast<DWORD>(from & 0xFFFFFFFFu);
            at.OffsetHigh = static_cast<DWORD>(from >> 32);
            const auto asked
                = static_cast<DWORD>(std::min<std::size_t>(size - done, std::numeric_limits<DWORD>::max()));
            DWORD bytesRead{};
            if (!ReadFile(nativeHandle, static_cast<char*>(data) + done, asked, &bytesRead, &at))
            {
                const DWORD error = GetLastError();
                if (error == ERROR_HANDLE_EOF)
                    break;
                return Err{ std::error_code(static_cast<std::int32_t>(error), std::system_category()) };
            }
            if (bytesRead == 0)
                break;
            done += bytesRead;
        }
        return done;
    }

    std::size_t read(Handle handle, void* data, std::size_t size)
    {
        const auto nativeHandle = getNativeHandle(handle);

        DWORD bytesRead{};
        size = std::min<std::size_t>(size, std::numeric_limits<DWORD>::max());
        if (!ReadFile(nativeHandle, data, static_cast<DWORD>(size), &bytesRead, nullptr))
            throw std::runtime_error(std::format("A read operation on a file failed: {}", GetLastError()));

        return bytesRead;
    }

    void makeStandardErrorBinary()
    {
        static_cast<void>(_setmode(_fileno(stderr), _O_BINARY));
    }

    bool namesIgnoreCase()
    {
        return true;
    }
}
