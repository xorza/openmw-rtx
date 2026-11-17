#include "raycast.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

namespace DetourNavigator
{
    std::optional<osg::Vec3f> raycast(const dtNavMeshQuery& navMeshQuery, const osg::Vec3f& halfExtents,
        const osg::Vec3f& start, const osg::Vec3f& end, const Flags includeFlags)
    {
        dtQueryFilter queryFilter;
        queryFilter.setIncludeFlags(includeFlags);

        dtPolyRef ref = 0;
        if (dtStatus status = navMeshQuery.findNearestPoly(start.ptr(), halfExtents.ptr(), &queryFilter, &ref, nullptr);
            dtStatusFailed(status) || ref == 0)
            return {};

        const std::uint32_t options = 0;
        std::array<dtPolyRef, 16> path{};
        dtRaycastHit hit{};
        hit.path = path.data();
        hit.maxPath = static_cast<std::int32_t>(path.size());
        if (dtStatus status = navMeshQuery.raycast(ref, start.ptr(), end.ptr(), &queryFilter, options, &hit);
            dtStatusFailed(status) || hit.pathCount == 0)
            return {};

        osg::Vec3f hitPosition;
        if (dtStatus status = navMeshQuery.closestPointOnPoly(
                path[static_cast<std::size_t>(hit.pathCount - 1)], end.ptr(), hitPosition.ptr(), nullptr);
            dtStatusFailed(status))
            return {};

        return hitPosition;
    }
}
