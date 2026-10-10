#include "kernelfilelinux.hpp"

#include <cerrno>
#include <cstddef>

#include <fcntl.h>
#include <unistd.h>

namespace Platform::KernelFile
{
    Read readOpened(const int descriptor, const std::span<char> buffer) noexcept
    {
        std::size_t length = 0;
        while (length < buffer.size())
        {
            const ssize_t count = ::read(descriptor, buffer.data() + length, buffer.size() - length);
            if (count == 0)
                return Read{ .mText = std::string_view(buffer.data(), length) };
            if (count > 0)
                length += static_cast<std::size_t>(count);
            else if (errno != EINTR)
                return Read{ .mText = std::string_view(buffer.data(), length), .mError = errno };
        }
        return Read{ .mText = std::string_view(buffer.data(), length), .mError = EFBIG };
    }

    std::optional<std::string_view> read(const char* const path, const std::span<char> buffer) noexcept
    {
        const int descriptor = open(path, O_RDONLY | O_CLOEXEC);
        if (descriptor == -1)
            return std::nullopt;
        const Read whole = readOpened(descriptor, buffer);
        close(descriptor);
        if (whole.mError != 0)
            return std::nullopt;
        return whole.mText;
    }
}
