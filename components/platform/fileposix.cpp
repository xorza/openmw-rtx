#include "file.hpp"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <components/files/conversion.hpp>

namespace Platform::File
{

    namespace
    {
        auto getNativeHandle(Handle handle)
        {
            assert(handle != Handle::Invalid);

            return static_cast<std::int32_t>(handle);
        }

        std::int32_t getNativeSeekType(SeekType seek)
        {
            if (seek == SeekType::Begin)
                return SEEK_SET;
            if (seek == SeekType::Current)
                return SEEK_CUR;
            if (seek == SeekType::End)
                return SEEK_END;
            return -1;
        }
    }

    Handle open(const std::filesystem::path& filename)
    {
        auto handle = ::open(filename.c_str(), O_RDONLY, 0);
        if (handle == -1)
        {
            throw std::system_error(errno, std::generic_category(),
                std::string("Failed to open '") + Files::pathToUnicodeString(filename) + "' for reading");
        }
        return static_cast<Handle>(handle);
    }

    void close(Handle handle)
    {
        auto nativeHandle = getNativeHandle(handle);

        ::close(nativeHandle);
    }

    void seek(Handle handle, std::size_t position, SeekType type /*= SeekType::Begin*/)
    {
        const auto nativeHandle = getNativeHandle(handle);
        const auto nativeSeekType = getNativeSeekType(type);

        if (::lseek(nativeHandle, static_cast<off_t>(position), nativeSeekType) == -1)
        {
            throw std::system_error(errno, std::generic_category(), "An lseek() call failed");
        }
    }

    std::size_t size(Handle handle)
    {
        const auto oldPos = tell(handle);

        seek(handle, 0, SeekType::End);
        const auto fileSize = tell(handle);
        seek(handle, oldPos, SeekType::Begin);

        return static_cast<std::size_t>(fileSize);
    }

    std::size_t tell(Handle handle)
    {
        auto nativeHandle = getNativeHandle(handle);

        const off_t position = ::lseek(nativeHandle, 0, SEEK_CUR);
        if (position == -1)
        {
            throw std::system_error(errno, std::generic_category(), "An lseek() call failed");
        }
        return static_cast<std::size_t>(position);
    }

    Result<std::size_t, std::error_code> readAt(
        Handle handle, std::uint64_t offset, void* data, std::size_t size) noexcept
    {
        const auto nativeHandle = getNativeHandle(handle);

        std::size_t done = 0;
        while (done < size)
        {
            const ssize_t amount = ::pread(
                nativeHandle, static_cast<char*>(data) + done, size - done, static_cast<off_t>(offset + done));
            if (amount == -1)
            {
                if (errno == EINTR)
                    continue;
                return Err{ std::error_code(errno, std::generic_category()) };
            }
            if (amount == 0)
                break;
            done += static_cast<std::size_t>(amount);
        }
        return done;
    }

    std::size_t read(Handle handle, void* data, std::size_t size)
    {
        auto nativeHandle = getNativeHandle(handle);

        auto amount = static_cast<std::int32_t>(::read(nativeHandle, data, size));
        if (amount == -1)
        {
            throw std::system_error(
                errno, std::generic_category(), "An attempt to read " + std::to_string(size) + " bytes failed");
        }
        return static_cast<std::size_t>(amount);
    }

    void makeStandardErrorBinary() {}

    bool namesIgnoreCase()
    {
        return false;
    }

}
