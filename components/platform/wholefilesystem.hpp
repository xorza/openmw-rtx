#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <system_error>

#include <components/misc/result.hpp>

#include "wholefile.hpp"

/// What `wholefile.cpp` asks of the system, which `wholefileposix.cpp` and `wholefilewin32.cpp`
/// each answer: the reads, writes, renames and syncs a whole file is made of.
namespace Platform::WholeFileSystem
{
    /// Where a read puts its bytes: `grow(buffer, bytes)` sizes the buffer to `bytes` and hands back
    /// its first byte.
    struct Sink
    {
        std::byte* (*mGrow)(void* buffer, std::size_t bytes);
        void* mBuffer;
    };

    /// Reads all of `path` into `sink`, to the end, and sizes it to what was read; a file past `most`
    /// bytes is `tooLarge()`, on its stated size before the sink grows where it states one.
    Result<void, IoError> read(const std::filesystem::path& path, Sink sink, std::size_t most);

    /// What a read past its bound fails with.
    inline Err<IoError> tooLarge()
    {
        return Err<IoError>{ IoError{
            .mFailure = IoFailure::TooLarge, .mCode = std::make_error_code(std::errc::file_too_large) } };
    }

    /// Makes `path`, which must not be there yet, holding `parts` one after another, with the
    /// permissions of `replacing` where that file stands, and syncs it before it is closed where
    /// `synced`.
    Result<void, IoError> create(const std::filesystem::path& path, std::span<const std::span<const std::byte>> parts,
        bool synced, const std::filesystem::path& replacing);

    /// Renames `from` over `to`, replacing a file there, and makes the rename durable where `synced`:
    /// `IoFailure::NotDurable` where the rename landed and that could not be confirmed.
    Result<void, IoError> replace(const std::filesystem::path& from, const std::filesystem::path& to, bool synced);

    /// Removes `path` where it is there; nothing is reported, since this cleans up after a failure
    /// already reported.
    void remove(const std::filesystem::path& path) noexcept;
}
