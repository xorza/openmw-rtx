#pragma once

#include <components/rtx/prepare/filestamps.hpp>
#include <components/vfs/pathutil.hpp>

namespace VFS
{
    class Manager;
}

namespace MWRender
{
    /// A file's stamp as the virtual file system holds it: the archive or folder that holds it and its time, so a file
    /// a later folder shadows, an archive rebuilt and a loose file written each move it (`Rtx::FileStamps`).
    class VfsStamps final : public Rtx::FileStamps
    {
    public:
        explicit VfsStamps(const VFS::Manager& vfs)
            : mVfs(vfs)
        {
        }

        Rtx::FileStamp stampOf(VFS::Path::NormalizedView path) override;

    private:
        const VFS::Manager& mVfs;
    };
}
