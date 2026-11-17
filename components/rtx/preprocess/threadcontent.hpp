#pragma once

#include <mutex>
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
#include "imagefactcache.hpp"
#include "texelreads.hpp"

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

        /// What every thread derived of a header's texels (`Resource::ImageHeader::derived`): the facts read so far,
        /// each by whichever thread asked first.
        struct SharedFacts : osg::Referenced
        {
            std::mutex mMutex;
            ImageFacts mFacts;
        };

        /// The fact `member` of `image`: where it is a header, as any thread read it already, and read off its texels
        /// and kept for every thread where none has, so a thread meeting a file the others read reads no texel
        /// again once they are let go of; read off `image` itself where it holds its texels.
        template <class Fact, class Read>
        Fact factOf(const osg::Image& image, std::optional<Fact> ImageFacts::* member, Read read)
        {
            const auto* const header = dynamic_cast<const Resource::ImageHeader*>(&image);
            if (header == nullptr)
                return read(image);

            auto& shared = header->derived<SharedFacts>();
            {
                const std::scoped_lock lock(shared.mMutex);
                if (const std::optional<Fact>& known = shared.mFacts.*member; known.has_value())
                    return *known;
            }
            const Fact made = read(*readTexels(image, TexelRead::Fact, mPreprocessor.texelReads()));
            const std::scoped_lock lock(shared.mMutex);
            shared.mFacts.*member = made;
            return made;
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
