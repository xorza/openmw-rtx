#include "navmeshtileview.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <tuple>

#include <DetourCommon.h>
#include <DetourNavMesh.h>

#include "ref.hpp"

// Found by argument-dependent lookup, which an unnamed namespace would hide.
// NOLINTBEGIN(misc-use-anonymous-namespace)
static bool operator==(const dtMeshHeader& lhs, const dtMeshHeader& rhs) noexcept
{
    const auto makeTuple = [](const dtMeshHeader& v) {
        using DetourNavigator::ArrayRef;
        return std::tuple(v.x, v.y, v.layer, v.userId, v.polyCount, v.vertCount, v.maxLinkCount, v.detailMeshCount,
            v.detailVertCount, v.detailTriCount, v.bvNodeCount, v.offMeshConCount, v.offMeshBase, v.walkableHeight,
            v.walkableRadius, v.walkableClimb, v.detailVertCount, ArrayRef(v.bmin), ArrayRef(v.bmax), v.bvQuantFactor);
    };
    return makeTuple(lhs) == makeTuple(rhs);
}

static bool operator==(const dtPoly& lhs, const dtPoly& rhs) noexcept
{
    const auto makeTuple = [](const dtPoly& v) {
        using DetourNavigator::ArrayRef;
        return std::tuple(ArrayRef(v.verts), ArrayRef(v.neis), v.flags, v.vertCount, v.areaAndtype);
    };
    return makeTuple(lhs) == makeTuple(rhs);
}

static bool operator==(const dtPolyDetail& lhs, const dtPolyDetail& rhs) noexcept
{
    const auto makeTuple
        = [](const dtPolyDetail& v) { return std::tuple(v.vertBase, v.triBase, v.vertCount, v.triCount); };
    return makeTuple(lhs) == makeTuple(rhs);
}

static bool operator==(const dtBVNode& lhs, const dtBVNode& rhs) noexcept
{
    const auto makeTuple = [](const dtBVNode& v) {
        using DetourNavigator::ArrayRef;
        return std::tuple(ArrayRef(v.bmin), ArrayRef(v.bmax), v.i);
    };
    return makeTuple(lhs) == makeTuple(rhs);
}

static bool operator==(const dtOffMeshConnection& lhs, const dtOffMeshConnection& rhs) noexcept
{
    const auto makeTuple = [](const dtOffMeshConnection& v) {
        using DetourNavigator::ArrayRef;
        return std::tuple(ArrayRef(v.pos), v.rad, v.poly, v.flags, v.side, v.userId);
    };
    return makeTuple(lhs) == makeTuple(rhs);
}
// NOLINTEND(misc-use-anonymous-namespace)

namespace DetourNavigator
{
    NavMeshTileConstView asNavMeshTileConstView(const std::uint8_t* data)
    {
        const auto* header = reinterpret_cast<const dtMeshHeader*>(data);

        if (header->magic != DT_NAVMESH_MAGIC)
            throw std::logic_error("Invalid navmesh magic");

        if (header->version != DT_NAVMESH_VERSION)
            throw std::logic_error("Invalid navmesh version");

        // Similar code to
        // https://github.com/recastnavigation/recastnavigation/blob/c5cbd53024c8a9d8d097a4371215e3342d2fdc87/Detour/Source/DetourNavMesh.cpp#L978-L996
        const std::int32_t headerSize = dtAlign4(sizeof(dtMeshHeader));
        const std::int32_t vertsSize
            = dtAlign4(static_cast<std::int32_t>(sizeof(float) * 3 * static_cast<std::size_t>(header->vertCount)));
        const std::int32_t polysSize
            = dtAlign4(static_cast<std::int32_t>(sizeof(dtPoly) * static_cast<std::size_t>(header->polyCount)));
        const std::int32_t linksSize
            = dtAlign4(static_cast<std::int32_t>(sizeof(dtLink) * static_cast<std::size_t>(header->maxLinkCount)));
        const std::int32_t detailMeshesSize = dtAlign4(
            static_cast<std::int32_t>(sizeof(dtPolyDetail) * static_cast<std::size_t>(header->detailMeshCount)));
        const std::int32_t detailVertsSize = dtAlign4(
            static_cast<std::int32_t>(sizeof(float) * 3 * static_cast<std::size_t>(header->detailVertCount)));
        const std::int32_t detailTrisSize = dtAlign4(
            static_cast<std::int32_t>(sizeof(std::uint8_t) * 4 * static_cast<std::size_t>(header->detailTriCount)));
        const std::int32_t bvtreeSize
            = dtAlign4(static_cast<std::int32_t>(sizeof(dtBVNode) * static_cast<std::size_t>(header->bvNodeCount)));
        const std::int32_t offMeshLinksSize = dtAlign4(
            static_cast<std::int32_t>(sizeof(dtOffMeshConnection) * static_cast<std::size_t>(header->offMeshConCount)));

        const std::uint8_t* ptr = data + headerSize;

        NavMeshTileConstView view{};

        view.mHeader = header;
        view.mVerts = dtGetThenAdvanceBufferPointer<const float>(ptr, static_cast<std::size_t>(vertsSize));
        view.mPolys = dtGetThenAdvanceBufferPointer<const dtPoly>(ptr, static_cast<std::size_t>(polysSize));
        ptr += linksSize;
        view.mDetailMeshes
            = dtGetThenAdvanceBufferPointer<const dtPolyDetail>(ptr, static_cast<std::size_t>(detailMeshesSize));
        view.mDetailVerts = dtGetThenAdvanceBufferPointer<const float>(ptr, static_cast<std::size_t>(detailVertsSize));
        view.mDetailTris
            = dtGetThenAdvanceBufferPointer<const std::uint8_t>(ptr, static_cast<std::size_t>(detailTrisSize));
        view.mBvTree = dtGetThenAdvanceBufferPointer<const dtBVNode>(ptr, static_cast<std::size_t>(bvtreeSize));
        view.mOffMeshCons
            = dtGetThenAdvanceBufferPointer<const dtOffMeshConnection>(ptr, static_cast<std::size_t>(offMeshLinksSize));

        return view;
    }

    NavMeshTileConstView asNavMeshTileConstView(const dtMeshTile& tile)
    {
        NavMeshTileConstView view{};

        view.mHeader = tile.header;
        view.mPolys = tile.polys;
        view.mVerts = tile.verts;
        view.mDetailMeshes = tile.detailMeshes;
        view.mDetailVerts = tile.detailVerts;
        view.mDetailTris = tile.detailTris;
        view.mBvTree = tile.bvTree;
        view.mOffMeshCons = tile.offMeshCons;

        return view;
    }

    bool operator==(const NavMeshTileConstView& lhs, const NavMeshTileConstView& rhs) noexcept
    {
        using DetourNavigator::Ref;
        using DetourNavigator::Span;
        const auto makeTuple = [](const DetourNavigator::NavMeshTileConstView& v) {
            return std::tuple(Ref(*v.mHeader), Span(v.mPolys, v.mHeader->polyCount),
                Span(v.mVerts, v.mHeader->vertCount), Span(v.mDetailMeshes, v.mHeader->detailMeshCount),
                Span(v.mDetailVerts, v.mHeader->detailVertCount), Span(v.mDetailTris, v.mHeader->detailTriCount),
                Span(v.mBvTree, v.mHeader->bvNodeCount), Span(v.mOffMeshCons, v.mHeader->offMeshConCount));
        };
        return makeTuple(lhs) == makeTuple(rhs);
    }
}
