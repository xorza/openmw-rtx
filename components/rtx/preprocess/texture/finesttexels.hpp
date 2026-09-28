#pragma once

#include <optional>

#include <components/rtx/image/texturedata.hpp>

namespace osg
{
    class Image;
}

namespace Rtx
{
    class ContentDigest;
    struct AlphaScratch;

    /// An image's finest level as a texture pass reads it — `describeFinest` — described once for
    /// both the key and the run that follows it.
    ///
    /// **The key is what the passes read and nothing else**: the format, the encoding, the finest
    /// level's extent and its bytes. Not the file's name, so one picture under two names is one
    /// entry; not the coarser levels, which no texture pass reads; and not the size OpenSceneGraph
    /// counts the whole image at, which differs between its builds for a block format with no
    /// chain. An image that does not describe is keyed as that, because what a pass answers for it
    /// does not depend on its bytes.
    class FinestTexels
    {
    public:
        explicit FinestTexels(AlphaScratch& scratch)
            : mScratch(scratch)
        {
        }

        /// Describes `image` into the scratch and adds what it came to to `digest`.
        void describe(const osg::Image& image, ContentDigest& digest);

        /// What the last `describe` found, spanning the scratch: nothing for an image no reader
        /// here decodes.
        const std::optional<TextureData>& get() const { return mFinest; }

        AlphaScratch& getScratch() const { return mScratch; }

    private:
        AlphaScratch& mScratch;
        std::optional<TextureData> mFinest;
    };
}
