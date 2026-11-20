#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

#include <osg/Referenced>
#include <osg/ref_ptr>

#include <components/resource/imageheader.hpp>
#include <components/rtx/preprocess/sourceindex.hpp>
#include <components/vfs/pathutil.hpp>

#include "vfsstamps.hpp"

namespace Rtx
{
    class ImageDerivation;
}

namespace MWRender
{
    /// **The renderer's headers, from a file's record where its stamp stands, and derived where a loader reads it
    /// otherwise** (`Resource::HeaderDeriver`): the records in the store's source index (`Rtx::SourceIndex`), what a
    /// read comes to by `Rtx::ImageDerivation`, filed in the index as it is derived, and the index written as this
    /// goes where anything moved. Asked from every loader's thread at once.
    class HeaderSources final : public Resource::HeaderDeriver
    {
    public:
        /// @param index the source index's file, or nothing for none: every header derived where its file is read.
        HeaderSources(const VFS::Manager& vfs, Rtx::ImageDerivation& derivation,
            const std::optional<std::filesystem::path>& index);

        HeaderSources(const HeaderSources&) = delete;
        HeaderSources& operator=(const HeaderSources&) = delete;

        /// Writes the index where anything moved, its records no run named for a month dropped.
        ~HeaderSources() override;

        Resource::HeaderLookup find(VFS::Path::NormalizedView path) override;

        osg::ref_ptr<const osg::Referenced> derive(const Read& read) override;

    private:
        VfsStamps mStamps;
        Rtx::ImageDerivation& mDerivation;
        std::optional<Rtx::SourceIndex> mIndex;

        /// The day this session names its records on, since the epoch.
        const std::uint32_t mDay;
    };
}
