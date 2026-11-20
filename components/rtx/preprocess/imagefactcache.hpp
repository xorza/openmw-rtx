#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

#include <components/rtx/image/imagefacts.hpp>
#include <components/vfs/pathutil.hpp>

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// The facts of every file that holds its own texels this has been asked about — a header carries its own —
    /// kept for the life of its thread's owner and keyed by the file, because a file's texels never change. Kept here
    /// and not beside the texture's slot, which the scene gives back when the last material naming the image goes: a
    /// spell's map goes with every burst, and a cache that died with the slot read every texel again on the next cast.
    /// One instance a thread, beside its `ContentPreprocessor`.
    class ImageFactCache
    {
    public:
        /// `image`'s facts under its file name, read by `read` the first time it is met. The reference stands for the
        /// life of this object. A named file alone: the texture table refuses an image with no name before any caller
        /// could ask of it. Allocates only for a file met the first time, because the ring's reader asks again for
        /// every material it reads.
        template <class Read>
        const ImageFacts& of(const osg::Image& image, Read&& read)
        {
            if (const ImageFacts* const known = find(image))
                return *known;
            return mByFile.emplace(VFS::Path::Normalized(mName), read()).first->second;
        }

        std::size_t size() const { return mByFile.size(); }

        /// Room for `files` entries, so meeting them rehashes nothing: what the frame's thread asks
        /// at attach for every texture the content holds, where a cache grown by what a frame met
        /// rehashed thousands of entries on the frame that crossed its bound.
        void reserve(std::size_t files) { mByFile.reserve(files); }

    private:
        /// `image`'s entry, its name normalised into `mName`; null where it has none.
        const ImageFacts* find(const osg::Image& image);

        std::unordered_map<VFS::Path::Normalized, ImageFacts, VFS::Path::Hash, std::equal_to<>> mByFile;

        /// The file name being looked for, normalised in place.
        std::string mName;
    };
}
