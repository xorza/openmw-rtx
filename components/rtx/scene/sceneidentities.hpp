#pragma once

#include <cstdint>

#include <components/rtx/common/ownedby.hpp>

namespace Rtx
{
    /// **Which scene description is which** (`SceneDesc::getIdentity`): opaque, compared and nothing else, a type of
    /// its own so no count or index passes for one. `SceneIdentity{}` is none, which no description holds.
    enum class SceneIdentity : std::uint64_t
    {
    };

    /// **Where every scene description's identity comes from** (`SceneDesc::getIdentity`): one source for every scene a
    /// renderer's slots may meet, kept by the renderer, so no two of them hold one identity, as a registry mints its
    /// entities' ids. Never nought, never handed out twice. Every description is made on the thread that made the
    /// source — the world's and each picture's on the main thread — which `OwnedBy` holds.
    class SceneIdentities
    {
    public:
        SceneIdentities() = default;

        SceneIdentities(const SceneIdentities&) = delete;
        SceneIdentities& operator=(const SceneIdentities&) = delete;
        ~SceneIdentities() = default;

        /// The next identity no description of this source has held.
        SceneIdentity next() noexcept
        {
            mOwner.check();
            ++mLast;
            return SceneIdentity{ mLast };
        }

    private:
        OwnedBy mOwner;
        std::uint64_t mLast = 0;
    };
}
