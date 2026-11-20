#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#include <components/misc/result.hpp>

namespace Platform::File
{

    enum class Handle : std::intptr_t
    {
        Invalid = -1
    };

    enum class SeekType
    {
        Begin,
        Current,
        End
    };

    Handle open(const std::filesystem::path& filename);

    void close(Handle handle);

    std::size_t size(Handle handle);

    void seek(Handle handle, std::size_t position, SeekType type = SeekType::Begin);
    std::size_t tell(Handle handle);

    std::size_t read(Handle handle, void* data, std::size_t size);

    /// Reads `size` bytes at `offset` into `data`, whatever the handle's own position: what any number of
    /// threads may do at once through one handle (POSIX `pread`, Windows `ReadFile` with the offset in an
    /// `OVERLAPPED`). Where the handle's position stands after is the system's, which Windows moves and POSIX
    /// leaves, so a handle read at an offset is read no other way.
    ///
    /// **Read until the bytes asked are in or the file ends**: a system may answer fewer than asked for no reason of
    /// the file's (a signal, a network volume, a request past what one call takes), so the read goes on from a short
    /// count and is made again where a signal interrupted it. Fewer bytes than asked are answered only at the file's
    /// end, as libstdc++'s `basic_filebuf::xsgetn` reads.
    ///
    /// **A failure is answered and not thrown**, as the system's own error on either system: a disk that errs is for
    /// the caller beside the read to go on from, a cache as a miss.
    Result<std::size_t, std::error_code> readAt(
        Handle handle, std::uint64_t offset, void* data, std::size_t size) noexcept;

    /// The standard error written byte for byte, as a binary log needs: Windows' text mode would turn each `\n` into
    /// `\r\n`; Linux writes bytes as they are already.
    void makeStandardErrorBinary();

    /// Whether the system's file names compare without case: Windows', and not Linux's.
    bool namesIgnoreCase();

    class ScopedHandle
    {
        Handle mHandle{ Handle::Invalid };

    public:
        ScopedHandle() noexcept = default;
        ScopedHandle(ScopedHandle& other) = delete;
        // A handle converts to the scoped handle that closes it.
        // NOLINTNEXTLINE(google-explicit-constructor)
        ScopedHandle(Handle handle) noexcept
            : mHandle(handle)
        {
        }
        ScopedHandle(ScopedHandle&& other) noexcept
            : mHandle(other.mHandle)
        {
            other.mHandle = Handle::Invalid;
        }
        ScopedHandle& operator=(const ScopedHandle& other) = delete;
        ScopedHandle& operator=(ScopedHandle&& other) noexcept
        {
            if (mHandle != Handle::Invalid)
                close(mHandle);
            mHandle = other.mHandle;
            other.mHandle = Handle::Invalid;
            return *this;
        }
        ~ScopedHandle()
        {
            if (mHandle != Handle::Invalid)
                close(mHandle);
        }

        // A file is used as its handle.
        // NOLINTNEXTLINE(google-explicit-constructor)
        operator Handle() const { return mHandle; }
    };
}
