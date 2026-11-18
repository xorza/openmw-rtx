#pragma once

#include <optional>

#include <osg/Referenced>

#include <components/contentstore/key.hpp>

#include "imagefactcache.hpp"

namespace Rtx
{
    /// What is derived of a file's texels where they are first read (`ContentPreprocessor::derive`): the key every
    /// image pass is keyed from (`FileKeys`), and every fact a material asks.
    struct FileFacts
    {
        ContentStore::Key mFile;

        /// Each fact set.
        ImageFacts mFacts;
    };

    /// A header's derivation (`Resource::ImageHeader::derived`), made by `ImageDerivation` before its manager hands
    /// the header out, and read by every thread after without a lock: nothing writes it once a second thread can
    /// see it.
    struct HeaderDerived : osg::Referenced
    {
        std::optional<FileFacts> mFacts;
    };
}
