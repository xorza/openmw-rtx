#pragma once

#include <string>

#include <osg/Image>

#include <components/contentstore/store.hpp>
#include <components/resource/imageheader.hpp>
#include <components/rtx/image/imagefacts.hpp>
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

        /// **Every fact of `image`'s texels**: a header's as derived where its file was first read (`ImageDerivation`),
        /// which reads nothing; an image that holds its own texels, as a model embeds one, read through the
        /// preprocessor the first time its file is asked and kept (`ImageFactCache`). The reference stands for the
        /// life of the header, or of this.
        const ImageFacts& factsOf(const osg::Image& image)
        {
            if (const auto* const header = dynamic_cast<const Resource::ImageHeader*>(&image))
                return header->derived<HeaderDerived>().getFacts().mFacts;
            return mFacts.of(image, [&] { return mPreprocessor.facts(image); });
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
