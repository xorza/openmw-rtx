#pragma once

#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/texturedata.hpp>

#include "ownedtexture.hpp"

namespace Rtx::Testing
{
    /// The whole chain of a texture whose file carried one level of more than a texel, built as the
    /// host builds it (`ChainBuild`) and laid out loose, as `ChainPass` lays out a small one. Owns its
    /// bytes, unlike a `TextureData`, because there is nobody else's to span for a level no file holds.
    class MipChain
    {
    public:
        MipChain() = default;

        /// For a caller with one texture to build and no chain to reuse. `build` is the whole of it.
        explicit MipChain(const TextureData& described) { build(described); }

        /// Builds a chain where the file carried one level of more than a texel, and nothing otherwise.
        /// Refills this one, so a loader keeps the room the last chain grew.
        void build(const TextureData& described);

        /// Empties the chain and keeps the room its texture grew.
        void reuse() { mTexture.reuse(); }

        /// Whether there was nothing to build, which is the ordinary case.
        bool isEmpty() const { return mTexture.isEmpty(); }

        /// What was built, spanning this object's own storage. Its slot is the caller's to fill in,
        /// exactly as `describeImage`'s is.
        TextureData describe() const;

    private:
        /// The host's chain, which this lays out as a texture.
        ChainBuild mChain;

        /// Every level, back to back, four bytes a texel, with the name and the format beside them.
        OwnedTexture mTexture;

        /// Whether what was decoded is display-encoded, which decides what the filter averages in
        /// and which format the description names.
        bool mEncoded = true;
    };
}
