#pragma once

#include <vector>

#include <osg/Vec3f>

#include <components/rtx/image/alphaimage.hpp>
#include <components/rtx/image/texturedata.hpp>

#include "ownedtexture.hpp"

namespace Rtx::Testing
{
    /// The levels a texture needs, built on the host where its file carried none
    /// (`TextureData::wantsCompletedChain`): the host's statement of the chain the device makes as
    /// the texture arrives, `mipchain.comp`, which a test holds that one to. Owns its bytes, unlike
    /// a `TextureData`, because there is nobody else's to span for a level no file holds.
    class MipChain
    {
    public:
        MipChain() = default;

        /// For a caller with one texture to build and no chain to reuse. `build` is the whole of it.
        explicit MipChain(const TextureData& described) { build(described); }

        /// Builds a chain where `TextureData::wantsCompletedChain` says, and nothing otherwise. Refills this one, so a
        /// loader keeps the room the last chain grew.
        void build(const TextureData& described);

        /// Empties the chain and keeps the room its texture grew.
        void reuse() { mTexture.reuse(); }

        /// Whether there was nothing to build, which is the ordinary case.
        bool isEmpty() const { return mTexture.isEmpty(); }

        /// What was built, spanning this object's own storage. Its slot is the caller's to fill in,
        /// exactly as `describeImage`'s is.
        TextureData describe() const;

    private:
        /// The finest level's alpha, read to weigh the colours by it. Not among what `build`
        /// resets, because nothing reads it but the line that fills it.
        AlphaImage mAlpha;

        /// A band of the finest level's colours at a time, `readTexelBand`'s.
        std::vector<osg::Vec3f> mBand;

        /// Every level, back to back, four bytes a texel, with the name and the format beside them.
        OwnedTexture mTexture;

        /// Whether what was decoded is display-encoded, which decides what the filter averages in
        /// and which format the description names.
        bool mEncoded = true;
    };
}
