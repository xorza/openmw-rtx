#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    /// What stands for a content file's version without reading it: the game's answers out of the
    /// virtual file system, the archive or folder that holds the file and its time (`ContentPreparation`).
    /// A stamp that moves is a file read again; one that lies costs a preparation it skipped, and
    /// never a wrong answer, since every lookup in the store is keyed by the bytes.
    class FileStamps
    {
    public:
        virtual ~FileStamps() = default;

        /// `path`'s stamp, or nought where nothing stands at it. Safe from any thread.
        virtual std::uint64_t stampOf(VFS::Path::NormalizedView path) = 0;
    };

    /// Another's stamps, each path's asked of it once and kept: a preparation names a file under many
    /// models — half a million names for a hundred thousand files on a large content list — and a
    /// stamp of the game's is a lookup, an archive's name and a file time. Bounded by the files the
    /// content holds. Safe from any thread.
    class KeptStamps final : public FileStamps
    {
    public:
        explicit KeptStamps(FileStamps& source)
            : mSource(source)
        {
        }

        std::uint64_t stampOf(VFS::Path::NormalizedView path) override;

    private:
        /// A map a `std::string_view` finds an entry of with no string made.
        struct NameHash
        {
            using is_transparent = void;
            std::size_t operator()(std::string_view name) const { return std::hash<std::string_view>{}(name); }
        };

        FileStamps& mSource;
        std::mutex mMutex;
        std::unordered_map<std::string, std::uint64_t, NameHash, std::equal_to<>> mKept;
    };
}
