#include "wholefile.hpp"

#include <atomic>
#include <format>

#include "process.hpp"
#include "wholefilesystem.hpp"

namespace Platform
{
    namespace
    {
        template <class Buffer>
        std::byte* grow(void* buffer, const std::size_t bytes)
        {
            Buffer& into = *static_cast<Buffer*>(buffer);
            into.resize(bytes);
            return reinterpret_cast<std::byte*>(into.data());
        }

        /// A name beside `path` that no other write of this process or another takes: the process's
        /// id and a count this process steps at every write.
        std::filesystem::path temporaryFor(const std::filesystem::path& path)
        {
            // The one count of the process's writes, so two threads writing one target in the same
            // instant take two names.
            static std::atomic<std::uint64_t> writes{ 0 };
            std::filesystem::path temporary = path;
            temporary += std::format(
                ".{}-{}{}", Process::currentId(), writes.fetch_add(1, std::memory_order_relaxed), sPartialSuffix);
            return temporary;
        }
    }

    Result<void, IoError> readWhole(const std::filesystem::path& path, Misc::ByteBuffer& into, const std::size_t most)
    {
        into.clear();
        return WholeFileSystem::read(
            path, WholeFileSystem::Sink{ .mGrow = &grow<Misc::ByteBuffer>, .mBuffer = &into }, most);
    }

    Result<void, IoError> readWhole(const std::filesystem::path& path, std::string& into, const std::size_t most)
    {
        into.clear();
        return WholeFileSystem::read(
            path, WholeFileSystem::Sink{ .mGrow = &grow<std::string>, .mBuffer = &into }, most);
    }

    Result<void, IoError> writeWhole(const std::filesystem::path& path,
        std::span<const std::span<const std::byte>> parts, const Durability durability)
    {
        const bool synced = durability == Durability::Synced;

        // **Through a link to the file it names**, as a write in place goes: renamed over the link
        // itself, a write would put a file in its place, and a settings file kept in a dotfiles
        // folder would part from it. A link that names nothing is written over.
        std::error_code unresolved;
        const std::filesystem::path linked = std::filesystem::is_symlink(path, unresolved)
            ? std::filesystem::canonical(path, unresolved)
            : std::filesystem::path();
        const std::filesystem::path& target = linked.empty() || unresolved ? path : linked;

        const std::filesystem::path temporary = temporaryFor(target);
        Result<void, IoError> written = WholeFileSystem::create(temporary, parts, synced);
        if (written.isOk())
            written = WholeFileSystem::replace(temporary, target, synced);
        if (!written.isOk())
            WholeFileSystem::remove(temporary);
        return written;
    }
}
