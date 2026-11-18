#pragma once

#include <atomic>
#include <cstdint>

namespace Rtx
{
    /// **Which scene description is which** (`SceneDesc::getIdentity`): opaque, compared and nothing else, a type of
    /// its own so no count or index passes for one. `SceneIdentity{}` is none, which no description holds.
    enum class SceneIdentity : std::uint64_t
    {
    };

    /// **Where every scene description's identity comes from** (`SceneDesc::getIdentity`): one source for every scene a
    /// renderer's slots may meet, kept by the renderer, so no two of them hold one identity, as a registry mints its
    /// entities' ids. Never nought, never handed out twice. Atomic, because a picture's description is made wherever a
    /// view is, and the tests make theirs on any thread.
    class SceneIdentities
    {
    public:
        SceneIdentities() = default;

        SceneIdentities(const SceneIdentities&) = delete;
        SceneIdentities& operator=(const SceneIdentities&) = delete;
        ~SceneIdentities() = default;

        /// The next identity no description of this source has held.
        SceneIdentity next() noexcept { return SceneIdentity{ mLast.fetch_add(1, std::memory_order_relaxed) + 1 }; }

    private:
        std::atomic<std::uint64_t> mLast{ 0 };
    };
}
