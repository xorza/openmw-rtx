#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <streambuf>

#include <components/platform/file.hpp>

namespace Files
{
    /// A file opened once and read by position, which every stream over it shares and which closes after the last.
    using SharedFile = std::shared_ptr<const Platform::File::ScopedHandle>;

    SharedFile openSharedFile(const std::filesystem::path& path);

    /// A file streambuf constrained to a specific region in the file, specified by the 'start' and 'length' parameters.
    /// It keeps its own position and reads by offset, so any number of them read one shared handle at once.
    class ConstrainedFileStreamBuf final : public std::streambuf
    {
    public:
        ConstrainedFileStreamBuf(SharedFile file, std::size_t start, std::size_t length);

        int_type underflow() final;

        /// What the buffer holds of `count` first, then the rest straight from the file into `into` where it is no
        /// less than the buffer, one read and one copy where the base read the buffer's 8 KiB a call and copied each
        /// twice: 2,731 reads for a 22 MiB image. A smaller rest goes through the buffer, as a short read does.
        std::streamsize xsgetn(char* into, std::streamsize count) final;

        pos_type seekoff(off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode) final;

        pos_type seekpos(pos_type pos, std::ios_base::openmode mode) final;

    private:
        pos_type seekTo(std::streamoff position, std::ios_base::openmode mode);

        SharedFile mFile;
        std::size_t mOrigin;
        std::size_t mSize;
        // Of the byte after the buffered ones, from the origin.
        std::size_t mPosition = 0;
        std::array<char, 8192> mBuffer{};
    };
}
