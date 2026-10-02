#pragma once

#include <cstddef>
#include <functional>
#include <optional>
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

    /// What a file's texels say, each fact read the first time it is asked and kept.
    ///
    /// **Two walks and not one.** Whether the alpha reaches solid stops at the first solid texel,
    /// which for a mask is its first block, and every blended material asks it. The mean decodes
    /// every texel's colour, and an additive sheet alone asks it. One walk for both decoded every
    /// blended texture the frame met: 11 ms of a frame at `seyda-neen-ship-dawn`.
    struct ImageFacts
    {
        /// `ContentPreprocessor::reachesSolid`.
        std::optional<bool> mReachesSolid;

        /// `ContentPreprocessor::meanTexel`.
        std::optional<MeanTexel> mMean;
    };

    /// The facts of every file this has been asked about, kept for the life of its thread's owner
    /// and keyed by the file, because a file's texels never change and a session meets the same
    /// maps under many materials. Kept here and not beside the texture's slot, which the scene gives
    /// back when the last material naming the image goes — and a spell's map goes with every burst,
    /// so a cache that died with the slot read every texel again on the frame of the next cast. One
    /// instance a thread, like its `ContentPreprocessor` — `ThreadContent`: the ring's reader has
    /// its own.
    class ImageFactCache
    {
    public:
        /// @param content this thread's, which reads a file's facts the first time each is asked.
        explicit ImageFactCache(ContentPreprocessor& content)
            : mContent(content)
        {
        }

        /// `image`'s entry under its file name, holding no fact until one is asked. The reference
        /// stands for the life of this object, so a caller that asks every frame keeps it and asks
        /// once. A named file alone: the texture table refuses an image with no name before any
        /// caller could ask of it.
        ImageFacts& of(const osg::Image& image);

        /// Whether `image`'s alpha ever reaches solid, read into `facts` — `image`'s entry — at
        /// the first ask.
        bool reachesSolid(ImageFacts& facts, const osg::Image& image);

        /// What a texel of `image` is worth on average, read into `facts` at the first ask.
        const MeanTexel& meanOf(ImageFacts& facts, const osg::Image& image);

        /// The same, for a caller that keeps no entry.
        const MeanTexel& meanOf(const osg::Image& image) { return meanOf(of(image), image); }

        std::size_t size() const { return mByFile.size(); }

    private:
        std::unordered_map<VFS::Path::Normalized, ImageFacts, VFS::Path::Hash, std::equal_to<>> mByFile;

        ContentPreprocessor& mContent;
    };
}
