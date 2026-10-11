#pragma once

#include <string>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/sceneutil/embeddedimage.hpp>
#include <components/vfs/pathutil.hpp>

#include "contentpreprocessor.hpp"
#include "imagefactcache.hpp"
#include "laidimagecache.hpp"

namespace Rtx
{
    /// What one thread computes from the content and keeps: the passes and what they cost, and the
    /// facts of every file met. **One a thread, owned by what owns the thread's walks**, so a
    /// picture walked on the frame's thread reads the world's caches and counts into the world's
    /// figures, rather than into a preprocessor of its own that nobody asks and that dies with the
    /// picture.
    ///
    /// **The two side by side, and neither holding the other**: the cache only finds a file's entry,
    /// and a fact is read here, by the preprocessor beside it. A cache that held its preprocessor
    /// could not be moved, and in an aggregate MSVC binds such a reference to the enclosing object.
    struct ThreadContent
    {
        ContentPreprocessor mPreprocessor;
        ImageFactCache mFacts;
        LaidImageCache mLaid;

        /// `ImageFactCache::of`.
        ImageFacts& factsOf(const osg::Image& image) { return mFacts.of(image); }

        /// `LaidImageCache::of`.
        osg::ref_ptr<const osg::Image> laidOf(const osg::Image& image) { return mLaid.of(image); }

        /// Whether `image`'s alpha ever reaches solid, read into `facts` — `image`'s entry — at
        /// the first ask.
        bool reachesSolid(ImageFacts& facts, const osg::Image& image)
        {
            if (!facts.mReachesSolid.has_value())
                facts.mReachesSolid = mPreprocessor.reachesSolid(image);
            return *facts.mReachesSolid;
        }

        /// What a texel of `image` is worth on average, read into `facts` at the first ask.
        const MeanTexel& meanOf(ImageFacts& facts, const osg::Image& image)
        {
            if (!facts.mMean.has_value())
                facts.mMean = mPreprocessor.meanTexel(image);
            return *facts.mMean;
        }

        /// The same, for a caller that keeps no entry.
        const MeanTexel& meanOf(const osg::Image& image) { return meanOf(factsOf(image), image); }

        /// `image`'s name (`SceneUtil::EmbeddedImage::nameOf`) normalised as the texture table
        /// normalises it, in a scratch kept and refilled, so taking a texture by its name reaches
        /// the heap only for a name longer than any before it: valid until the next call. A
        /// `VFS::Path::Normalized` apiece was a string at every material that took a texture.
        VFS::Path::NormalizedView pathOf(const osg::Image& image)
        {
            mPath.assign(SceneUtil::EmbeddedImage::nameOf(image));
            VFS::Path::normalizeFilenameInPlace(mPath);
            return VFS::Path::NormalizedView(mPath.c_str());
        }

        std::string mPath;
    };
}
