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

        /// Textures not asked yet: the feed out of credit, no room for a bake or a composite, or what one is made of
        /// not standing.
        std::uint32_t mUnasked = 0;

        /// Meshes placing nothing.
        std::uint32_t mMeshes = 0;

        constexpr std::uint32_t total() const { return mAsked + mArrived + mUnasked + mMeshes; }

        bool operator==(const MissingDemanded& other) const = default;
    };
}
