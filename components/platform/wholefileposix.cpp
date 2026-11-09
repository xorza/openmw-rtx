#include "wholefilesystem.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "uniquehold.hpp"

namespace Platform::WholeFileSystem
{
    namespace
    {
        struct Descriptor
        {
            using Handle = int;
            static constexpr Handle sNone = -1;
            static void close(Handle descriptor) noexcept { ::close(descriptor); }
        };

        using HeldDescriptor = UniqueHold<Descriptor>;

        IoError errorOf(const int code)
        {
            const auto failure = [code] {
                switch (code)
                {
                    case ENOENT:
                    case ENOTDIR:
                        return IoFailure::NotFound;
                    case EACCES:
                    case EPERM:
                    case EROFS:
                    case ETXTBSY:
                        return IoFailure::Denied;
                    case ENOSPC:
                    case EDQUOT:
                        return IoFailure::NoSpace;
                    default:
                        return IoFailure::Other;
                }
            }();
            return IoError{ .mFailure = failure, .mCode = std::error_code(code, std::system_category()) };
        }

        Err<IoError> lastError()
        {
            return Err<IoError>{ errorOf(errno) };
        }

        /// The size a read starts from where the system states none: what `/proc` and `/sys` hand
        /// out at once.
        constexpr std::size_t sFirstRead = 4096;
    }

    Result<void, IoError> read(const std::filesystem::path& path, const Sink sink, const std::size_t most)
    {
        const HeldDescriptor file(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
        if (!file.isOpen())
            return lastError();

        struct stat status
        {
        };
        if (fstat(file.get(), &status) != 0)
            return lastError();

        const auto stated = static_cast<std::size_t>(std::max<off_t>(status.st_size, 0));
        if (stated > most)
            return tooLarge();

        // Sized to what the system states and one more, so a file that ends where it said is read to
        // its end by one read past it; grown by half where it runs on, as `/proc` files do.
        std::size_t capacity = std::max<std::size_t>(stated + 1, sFirstRead);
        std::size_t filled = 0;
        std::byte* data = sink.mGrow(sink.mBuffer, capacity);
        while (true)
        {
            if (filled == capacity)
            {
                capacity += capacity / 2;
                data = sink.mGrow(sink.mBuffer, capacity);
            }

            const ssize_t got = ::read(file.get(), data + filled, capacity - filled);
            if (got < 0 && errno == EINTR)
                continue;
            if (got < 0)
                return lastError();
            if (got == 0)
                break;
            filled += static_cast<std::size_t>(got);
            if (filled > most)
                return tooLarge();
        }

        sink.mGrow(sink.mBuffer, filled);
        return {};
    }

    Result<void, IoError> create(
        const std::filesystem::path& path, const std::span<const std::span<const std::byte>> parts, const bool synced)
    {
        const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (descriptor < 0)
            return lastError();

        const auto writeAll = [&]() -> Result<void, IoError> {
            for (std::span<const std::byte> part : parts)
                while (!part.empty())
                {
                    const ssize_t written = ::write(descriptor, part.data(), part.size());
                    if (written < 0 && errno == EINTR)
                        continue;
                    if (written < 0)
                        return lastError();
                    part = part.subspan(static_cast<std::size_t>(written));
                }
            if (synced && fsync(descriptor) != 0)
                return lastError();
            return {};
        };

        // Closed and judged here, since a close can report a write the system deferred.
        Result<void, IoError> written = writeAll();
        if (::close(descriptor) != 0 && written.isOk())
            written = lastError();
        return written;
    }

    Result<void, IoError> replace(const std::filesystem::path& from, const std::filesystem::path& to, const bool synced)
    {
        if (std::rename(from.c_str(), to.c_str()) != 0)
            return lastError();

        if (!synced)
            return {};

        // **The folder synced after the rename**, or a power loss can keep the old name: the rename
        // is the folder's write, not the file's.
        const std::filesystem::path folder = to.has_parent_path() ? to.parent_path() : std::filesystem::path(".");
        const HeldDescriptor held(::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (!held.isOpen())
            return lastError();
        if (fsync(held.get()) != 0)
            return lastError();
        return {};
    }

    void remove(const std::filesystem::path& path) noexcept
    {
        ::unlink(path.c_str());
    }
}
