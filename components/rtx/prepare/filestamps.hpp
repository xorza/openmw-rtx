#pragma once

#include <cstdint>

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
}
