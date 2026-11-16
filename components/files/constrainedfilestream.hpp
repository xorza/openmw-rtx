#pragma once

#include "constrainedfilestreambuf.hpp"
#include "istreamptr.hpp"
#include "streamwithbuffer.hpp"

#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <utility>

namespace Files
{

    /// A file stream constrained to a specific region in the file, specified by the 'start' and 'length' parameters.
    using ConstrainedFileStream = StreamWithBuffer<ConstrainedFileStreamBuf>;

    /// A stream over `length` bytes of a shared file from `start`, which reads alongside every other stream over it.
    inline IStreamPtr openConstrainedFileStream(SharedFile file, std::size_t start, std::size_t length)
    {
        return std::make_unique<ConstrainedFileStream>(
            std::make_unique<ConstrainedFileStreamBuf>(std::move(file), start, length));
    }

    /// A stream over a file of its own, from `start` to the file's end unless `length` says otherwise.
    inline IStreamPtr openConstrainedFileStream(const std::filesystem::path& filename, std::size_t start = 0,
        std::size_t length = std::numeric_limits<std::size_t>::max())
    {
        SharedFile file = openSharedFile(filename);
        if (length == std::numeric_limits<std::size_t>::max())
            length = Platform::File::size(*file) - start;
        return openConstrainedFileStream(std::move(file), start, length);
    }
}
