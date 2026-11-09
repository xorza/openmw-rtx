#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

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

    /// Reads all of `path` into `sink`, to the end, and sizes it to what was read.
    Result<void, IoError> read(const std::filesystem::path& path, Sink sink);

    /// Makes `path`, which must not be there yet, holding `parts` one after another, and syncs it
    /// before it is closed where `synced`.
    Result<void, IoError> create(
        const std::filesystem::path& path, std::span<const std::span<const std::byte>> parts, bool synced);

    /// Renames `from` over `to`, replacing a file there, and makes the rename durable where `synced`.
    Result<void, IoError> replace(const std::filesystem::path& from, const std::filesystem::path& to, bool synced);

    /// Removes `path` where it is there; nothing is reported, since this cleans up after a failure
    /// already reported.
    void remove(const std::filesystem::path& path) noexcept;
}
