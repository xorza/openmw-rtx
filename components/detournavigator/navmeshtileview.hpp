#pragma once

#include <cstdint>

struct dtMeshHeader;
struct dtPoly;
struct dtPolyDetail;
struct dtBVNode;
struct dtOffMeshConnection;
struct dtMeshTile;

namespace DetourNavigator
{
    struct NavMeshTileConstView
    {
        const dtMeshHeader* mHeader;
        const dtPoly* mPolys;
        const float* mVerts;
        const dtPolyDetail* mDetailMeshes;
        const float* mDetailVerts;
        const std::uint8_t* mDetailTris;
        const dtBVNode* mBvTree;
        const dtOffMeshConnection* mOffMeshCons;

        friend bool operator==(const NavMeshTileConstView& lhs, const NavMeshTileConstView& rhs) noexcept;
    };

    NavMeshTileConstView asNavMeshTileConstView(const std::uint8_t* data);
    NavMeshTileConstView asNavMeshTileConstView(const dtMeshTile& tile);
}
