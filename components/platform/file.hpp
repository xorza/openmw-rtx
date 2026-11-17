#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>

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

    /// Reads up to `size` bytes at `offset` into `data`, whatever the handle's own position: what any number of
    /// threads may do at once through one handle (POSIX `pread`, Windows `ReadFile` with the offset in an
    /// `OVERLAPPED`). Where the handle's position stands after is the system's, which Windows moves and POSIX
    /// leaves, so a handle read at an offset is read no other way. Fewer bytes than asked only at the file's end.
    std::size_t readAt(Handle handle, std::size_t offset, void* data, std::size_t size);

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
