#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Rtx
{
    /// The most distances `RingStanding` tells apart: the eye's own cell out to the fourteenth ring, and every cell
    /// past it together in the last.
    inline constexpr std::size_t sRingDistances = 16;

    /// What the ring holds at one distance from the eye's cell, in cells by the larger of the two axes, against what of
    /// it stands in the top level: its placements, and the meshes the ring adopted for them, each mesh counted at the
    /// nearest cell holding it and standing where any of its placements stands, with their vertices.
    struct RingDistance
    {
        std::uint32_t mPlacements = 0;
        std::uint32_t mPlacementsStanding = 0;
        std::uint32_t mMeshes = 0;
        std::uint32_t mMeshesStanding = 0;
        std::uint64_t mVertices = 0;
        std::uint64_t mVerticesStanding = 0;
    };

    /// What the ring holds by distance against what stands (`CellPlacer::countStanding`): what was read, adopted and
    /// built for placements the size rule, the active grid or a script keeps out.
    struct RingStanding
    {
        std::array<RingDistance, sRingDistances> mByDistance{};
    };
}
