#pragma once

#include <cassert>
#include <optional>
#include <string>

#include <osg/Image>
#include <osg/Referenced>
#include <osg/ref_ptr>

#include <components/contentstore/store.hpp>
#include <components/resource/imageheader.hpp>
#include <components/rtx/image/alpharange.hpp>
#include <components/sceneutil/embeddedimage.hpp>
#include <components/vfs/pathutil.hpp>

#include "contentpreprocessor.hpp"
#include "headerderived.hpp"
#include "imagefactcache.hpp"

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
        explicit ThreadContent(
            ContentStore::Store* store = nullptr, ContentStore::Reach reach = ContentStore::Reach::Memory)
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
                facts.mReachesSolid = factOf(image, &ImageFacts::mReachesSolid,
                    [&](const osg::Image& texels) { return mPreprocessor.reachesSolid(texels); });
            return *facts.mReachesSolid;
        }

        /// Whether `image`'s alpha ever falls below solid, read into `facts` at the first ask.
        bool fallsBelowSolid(ImageFacts& facts, const osg::Image& image)
        {
            if (!facts.mFallsBelowSolid.has_value())
                facts.mFallsBelowSolid = factOf(image, &ImageFacts::mFallsBelowSolid,
                    [&](const osg::Image& texels) { return mPreprocessor.fallsBelowSolid(texels); });
            return *facts.mFallsBelowSolid;
        }

        /// The lowest and the highest alpha `image` holds, read into `facts` at the first ask.
        AlphaRange alphaRangeOf(ImageFacts& facts, const osg::Image& image)
        {
            if (!facts.mAlphaRange.has_value())
                facts.mAlphaRange = factOf(image, &ImageFacts::mAlphaRange,
                    [&](const osg::Image& texels) { return mPreprocessor.alphaRange(texels); });
            return *facts.mAlphaRange;
        }

        /// What `image`'s channels mean as a `_spec` map, read into `facts` at the first ask.
        SpecularLayout specularLayoutOf(ImageFacts& facts, const osg::Image& image)
        {
            if (!facts.mSpecularLayout.has_value())
                facts.mSpecularLayout = factOf(image, &ImageFacts::mSpecularLayout,
                    [&](const osg::Image& texels) { return mPreprocessor.specularLayout(texels); });
            return *facts.mSpecularLayout;
        }

        /// What a texel of `image` is worth on average, read into `facts` at the first ask.
        const MeanTexel& meanOf(ImageFacts& facts, const osg::Image& image)
        {
            if (!facts.mMean.has_value())
                facts.mMean = factOf(image, &ImageFacts::mMean,
                    [&](const osg::Image& texels) { return mPreprocessor.meanTexel(texels); });
            return *facts.mMean;
        }

        /// The same, for a caller that keeps no entry.
        const MeanTexel& meanOf(const osg::Image& image) { return meanOf(factsOf(image), image); }

        /// The fact `member` of `image`: where it is a header, as derived where its file was first read
        /// (`ImageDerivation`), which reads nothing; read off `image` itself where it holds its texels.
        template <class Fact, class Read>
        Fact factOf(const osg::Image& image, std::optional<Fact> ImageFacts::* member, Read read)
        {
            const auto* const header = dynamic_cast<const Resource::ImageHeader*>(&image);
            if (header == nullptr)
                return read(image);

            const std::optional<FileFacts>& derived = header->derived<HeaderDerived>().mFacts;
            assert(derived.has_value() && "a header its manager handed out underived");
            const std::optional<Fact>& fact = derived->mFacts.*member;
            assert(fact.has_value() && "a derivation short of a fact");
            return *fact;
        }

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
