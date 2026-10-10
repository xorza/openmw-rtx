#pragma once

#include <optional>
#include <span>
#include <string_view>

/// Files the Linux kernel writes about a process and its machine, read by the system calls and never
/// through a stream: libstdc++'s file buffer throws where a read fails after the open, as a process
/// reaped since the open or a kernel short of memory fails it. Linux's alone, and built there alone.
namespace Platform::KernelFile
{
    /// What a read of a whole file came to.
    struct Read
    {
        /// What was read, at the start of the buffer.
        std::string_view mText;

        /// The `errno` of the read that failed; `EFBIG` where the text filled the buffer, which leaves
        /// unknown whether more followed; nought where the text was read to its end.
        int mError = 0;
    };

    /// The rest of `descriptor`, from where it stands, into `buffer`.
    Read readOpened(int descriptor, std::span<char> buffer) noexcept;

    /// The whole of the file at `path`, into `buffer`: nothing where it cannot be opened or read, or
    /// does not fit.
    std::optional<std::string_view> read(const char* path, std::span<char> buffer) noexcept;
}
