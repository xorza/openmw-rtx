#include "file.hpp"

#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <string>
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

            return static_cast<int>(handle);
        }

        int getNativeSeekType(SeekType seek)
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

    void seek(Handle handle, size_t position, SeekType type /*= SeekType::Begin*/)
    {
        const auto nativeHandle = getNativeHandle(handle);
        const auto nativeSeekType = getNativeSeekType(type);

        if (::lseek(nativeHandle, static_cast<off_t>(position), nativeSeekType) == -1)
        {
            throw std::system_error(errno, std::generic_category(), "An lseek() call failed");
        }
    }

    size_t size(Handle handle)
    {
        const auto oldPos = tell(handle);

        seek(handle, 0, SeekType::End);
        const auto fileSize = tell(handle);
        seek(handle, oldPos, SeekType::Begin);

        return static_cast<size_t>(fileSize);
    }

    size_t tell(Handle handle)
    {
        auto nativeHandle = getNativeHandle(handle);

        const off_t position = ::lseek(nativeHandle, 0, SEEK_CUR);
        if (position == -1)
        {
            throw std::system_error(errno, std::generic_category(), "An lseek() call failed");
        }
        return static_cast<size_t>(position);
    }

    size_t readAt(Handle handle, size_t offset, void* data, size_t size)
    {
        const auto nativeHandle = getNativeHandle(handle);

        const ssize_t amount = ::pread(nativeHandle, data, size, static_cast<off_t>(offset));
        if (amount == -1)
        {
            throw std::system_error(errno, std::generic_category(),
                "An attempt to read " + std::to_string(size) + " bytes at " + std::to_string(offset) + " failed");
        }
        return static_cast<size_t>(amount);
    }

    size_t read(Handle handle, void* data, size_t size)
    {
        auto nativeHandle = getNativeHandle(handle);

        int amount = static_cast<int>(::read(nativeHandle, data, size));
        if (amount == -1)
        {
            throw std::system_error(
                errno, std::generic_category(), "An attempt to read " + std::to_string(size) + " bytes failed");
        }
        return static_cast<std::size_t>(amount);
    }

}
