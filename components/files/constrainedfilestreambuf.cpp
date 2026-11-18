#include "constrainedfilestreambuf.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <ios>
#include <memory>
#include <utility>

namespace Files
{
    namespace File = Platform::File;

    SharedFile openSharedFile(const std::filesystem::path& path)
    {
        File::ScopedHandle handle = File::open(path);
        return std::make_shared<const File::ScopedHandle>(std::move(handle));
    }

    ConstrainedFileStreamBuf::ConstrainedFileStreamBuf(SharedFile file, std::size_t start, std::size_t length)
        : mFile(std::move(file))
        , mOrigin(start)
        , mSize(length)
    {
        setg(nullptr, nullptr, nullptr);
    }

    std::streambuf::int_type ConstrainedFileStreamBuf::underflow()
    {
        if (gptr() == egptr())
        {
            const std::size_t toRead = std::min(mSize - mPosition, mBuffer.size());
            // A file cut short since the entry was indexed reads as the entry's end.
            const std::size_t got = toRead == 0 ? 0 : File::readAt(*mFile, mOrigin + mPosition, mBuffer.data(), toRead);
            mPosition += got;
            setg(mBuffer.data(), mBuffer.data(), mBuffer.data() + got);
        }
        if (gptr() == egptr())
            return traits_type::eof();

        return traits_type::to_int_type(*gptr());
    }

    std::streamsize ConstrainedFileStreamBuf::xsgetn(char* const into, const std::streamsize count)
    {
        const std::streamsize buffered = std::min<std::streamsize>(egptr() - gptr(), count);
        if (buffered > 0)
        {
            std::copy_n(gptr(), buffered, into);
            // At most the buffer's 8 KiB, which an int holds.
            gbump(static_cast<std::int32_t>(buffered));
        }
        const std::streamsize rest = count - buffered;
        if (std::cmp_less(rest, mBuffer.size()))
            return buffered + std::streambuf::xsgetn(into + buffered, rest);

        const std::size_t wanted = std::min(static_cast<std::size_t>(rest), mSize - mPosition);
        const std::size_t got = wanted == 0 ? 0 : File::readAt(*mFile, mOrigin + mPosition, into + buffered, wanted);
        mPosition += got;
        // Nothing buffered past the read: the next read fetches from where this one ended.
        setg(nullptr, nullptr, nullptr);
        return buffered + static_cast<std::streamsize>(got);
    }

    std::streambuf::pos_type ConstrainedFileStreamBuf::seekoff(
        off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode)
    {
        switch (whence)
        {
            case std::ios_base::beg:
                return seekTo(offset, mode);
            case std::ios_base::cur:
                return seekTo(static_cast<std::streamoff>(mPosition) - (egptr() - gptr()) + offset, mode);
            case std::ios_base::end:
                return seekTo(static_cast<std::streamoff>(mSize) + offset, mode);
            default:
                return static_cast<pos_type>(static_cast<off_type>(-1));
        }
    }

    std::streambuf::pos_type ConstrainedFileStreamBuf::seekpos(pos_type pos, std::ios_base::openmode mode)
    {
        return seekTo(pos, mode);
    }

    std::streambuf::pos_type ConstrainedFileStreamBuf::seekTo(std::streamoff position, std::ios_base::openmode mode)
    {
        // A position before the start is refused as one past the end is.
        if ((mode & std::ios_base::out) || !(mode & std::ios_base::in) || position < 0
            || std::cmp_greater(position, mSize))
            return static_cast<pos_type>(static_cast<off_type>(-1));

        mPosition = static_cast<std::size_t>(position);
        // The next read fetches from the new position.
        setg(nullptr, nullptr, nullptr);
        return position;
    }
}
