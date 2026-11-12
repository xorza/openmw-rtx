#pragma once

#include <string>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/image/alpharange.hpp>
#include <components/sceneutil/embeddedimage.hpp>
#include <components/vfs/pathutil.hpp>

#include "contentpreprocessor.hpp"
#include "imagefactcache.hpp"
#include <components/contentstore/store.hpp>

namespace Rtx
{
    /// What one thread computes from the content and keeps: the passes and what they cost, and the
    /// facts of every file met. **One a thread, owned by what owns the thread's walks**, so a
    /// picture walked on the frame's thread counts into the world's figures and reads the world's
    /// caches, not those of a preprocessor of its own.
    ///
    /// **The two side by side, and neither holding the other**: the cache only finds a file's entry,
    /// and a fact is read here, by the preprocessor beside it. A cache that held its preprocessor
    /// could not be moved, and in an aggregate MSVC binds such a reference to the enclosing object.
    struct ThreadContent
    {
        /// @param store the process's store, or null for none (`ContentCache`).
        /// @param reach where this thread's asks may reach in it: memory alone on the frame's.
        ThreadContent(ContentStore::Store* store = nullptr, ContentStore::Reach reach = ContentStore::Reach::Memory)
            : mPreprocessor(store, reach)
        {
        }

        ContentPreprocessor mPreprocessor;
        ImageFactCache mFacts;

        /// `ImageFactCache::of`.
        ImageFacts& factsOf(const osg::Image& image) { return mFacts.of(image); }

        /// Whether `image`'s alpha ever reaches solid, read into `facts` — `image`'s entry — at
        /// the first ask.
        bool reachesSolid(ImageFacts& facts, const osg::Image& image)
        {
            if (!facts.mReachesSolid.has_value())
                facts.mReachesSolid = mPreprocessor.reachesSolid(image);
            return *facts.mReachesSolid;
        }

        /// Whether `image`'s alpha ever falls below solid, read into `facts` at the first ask.
        bool fallsBelowSolid(ImageFacts& facts, const osg::Image& image)
        {
            if (!facts.mFallsBelowSolid.has_value())
                facts.mFallsBelowSolid = mPreprocessor.fallsBelowSolid(image);
            return *facts.mFallsBelowSolid;
        }

        /// The lowest and the highest alpha `image` holds, read into `facts` at the first ask.
        AlphaRange alphaRangeOf(ImageFacts& facts, const osg::Image& image)
        {
            if (!facts.mAlphaRange.has_value())
                facts.mAlphaRange = mPreprocessor.alphaRange(image);
            return *facts.mAlphaRange;
        }

        /// What `image`'s channels mean as a `_spec` map, read into `facts` at the first ask.
        SpecularLayout specularLayoutOf(ImageFacts& facts, const osg::Image& image)
        {
            if (!facts.mSpecularLayout.has_value())
                facts.mSpecularLayout = mPreprocessor.specularLayout(image);
            return *facts.mSpecularLayout;
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
        /// the heap only for a name longer than any before it: valid until the next call.
        VFS::Path::NormalizedView pathOf(const osg::Image& image)
        {
            mPath.assign(SceneUtil::EmbeddedImage::nameOf(image));
            VFS::Path::normalizeFilenameInPlace(mPath);
            return VFS::Path::NormalizedView(mPath.c_str());
        }

        std::string mPath;
    };
}
