#pragma once

#include <cstddef>
#include <functional>
#include <unordered_map>

#include <components/rtx/image/texels.hpp>
#include <components/vfs/pathutil.hpp>

namespace osg
{
    class Image;
}

namespace Rtx
{
    class ContentPreprocessor;

    /// The mean texel of every map this has been asked about, kept for the life of its thread's
    /// owner and keyed by the file, because a file's mean never changes and a session meets tens of
    /// additive maps. Kept here and not beside the texture's slot, which the scene gives back when
    /// the last material naming the image goes — and a spell's map goes with every burst, so a
    /// cache that died with the slot read every texel again on the frame of the next cast. One
    /// instance a thread, like its `ContentPreprocessor` — `ThreadContent`: the ring's reader has
    /// its own.
    class MeanTexels
    {
    public:
        /// @param content this thread's, which reads a file's mean the first time it is asked.
        explicit MeanTexels(ContentPreprocessor& content)
            : mContent(content)
        {
        }

        /// `image`'s mean, read at the first ask under its file name and found at every ask after.
        /// The reference stands for the life of this object, so a caller that asks every frame
        /// keeps it and asks once. A named file alone: the texture table refuses an image with no
        /// name before any caller could ask of it.
        const MeanTexel& of(const osg::Image& image);

        std::size_t size() const { return mByFile.size(); }

    private:
        std::unordered_map<VFS::Path::Normalized, MeanTexel, VFS::Path::Hash, std::equal_to<>> mByFile;

        ContentPreprocessor& mContent;
    };
}
