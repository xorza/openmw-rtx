#pragma once

#include <osg/Referenced>

#include <components/contentstore/key.hpp>
#include <components/rtx/image/imagefacts.hpp>

namespace Rtx
{
    /// What is derived of a file's texels where they are first read (`ContentPreprocessor::derive`): the key every
    /// image pass is keyed from (`FileKeys`), and every fact a material asks.
    struct FileFacts
    {
        ContentStore::Key mFile;
        ImageFacts mFacts;
    };

    /// **A header's derivation** (`Resource::ImageHeader::derived`), made by `ImageDerivation` before its manager
    /// makes the header, and read by every thread after without a lock: nothing writes it once it is made.
    class HeaderDerived final : public osg::Referenced
    {
    public:
        explicit HeaderDerived(const FileFacts& facts)
            : mFacts(facts)
        {
        }

        const FileFacts& getFacts() const { return mFacts; }

    private:
        ~HeaderDerived() override = default;

        const FileFacts mFacts;
    };
}
