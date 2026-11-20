#pragma once

#include <cstdint>

namespace Rtx
{
    /// **Residents of a demanded tier the plan gives something that stand nothing yet, by why**: what a frame that
    /// draws the whole world has none of, where a raise still on its way draws what stands below it, and what a wait
    /// that stalls names (`Residency`).
    struct MissingDemanded
    {
        /// Textures whose texels were asked of the feed and have not come.
        std::uint32_t mAsked = 0;

        /// Textures whose texels came and wait for room in the arena or for the upload's share.
        std::uint32_t mArrived = 0;

        /// Textures not asked yet: a file's while the feed is out of credit; a sprite's light bake while its source
        /// stands at another level; a ground composite while a layer stands nothing; a bake or a composite ready to be
        /// made with no room for it.
        std::uint32_t mUnaskedFiles = 0;
        std::uint32_t mUnaskedBakes = 0;
        std::uint32_t mUnaskedComposites = 0;
        std::uint32_t mUnaskedForRoom = 0;

        /// Meshes placing nothing.
        std::uint32_t mMeshes = 0;

        constexpr std::uint32_t textures() const
        {
            return mAsked + mArrived + mUnaskedFiles + mUnaskedBakes + mUnaskedComposites + mUnaskedForRoom;
        }

        constexpr std::uint32_t total() const { return textures() + mMeshes; }

        bool operator==(const MissingDemanded& other) const = default;
    };
}
