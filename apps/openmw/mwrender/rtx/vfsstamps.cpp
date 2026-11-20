#include "vfsstamps.hpp"

#include <filesystem>
#include <span>
#include <string>

#include <components/contentstore/key.hpp>
#include <components/vfs/manager.hpp>

namespace MWRender
{
    Rtx::FileStamp VfsStamps::stampOf(const VFS::Path::NormalizedView path)
    {
        if (!mVfs.exists(path))
            return Rtx::FileStamp{};
        const std::string archive = mVfs.getArchive(VFS::Path::Normalized(path));
        const std::filesystem::file_time_type modified = mVfs.getLastModified(path);
        ContentStore::Digest digest("stamp", 1);
        digest.add(std::span<const char>(archive));
        digest.add(modified.time_since_epoch().count());
        return Rtx::FileStamp{ .mStamp = digest.getKey().mHash[0], .mModified = modified };
    }
}
