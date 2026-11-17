#include "uvdensity.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>

namespace Rtx
{
    namespace
    {
        /// The smallest ratio over the triangles of one set, as `coneBaseOf` reads each: where either area is not
        /// greater than nought, the shader reads the finest level, which a ratio of nought states.
        float sparsestOf(const std::span<const osg::Vec3f> positions, const std::span<const osg::Vec2f> coords,
            const std::span<const std::uint32_t> indices)
        {
            if (indices.empty())
                return std::numeric_limits<float>::infinity();
            if (coords.empty())
                return 0.0f;

            float sparsest = std::numeric_limits<float>::infinity();
            for (std::size_t at = 0; at < indices.size(); at += 3)
            {
                const std::uint32_t a = indices[at];
                const std::uint32_t b = indices[at + 1];
                const std::uint32_t c = indices[at + 2];

                // The shader's own expressions: the cross product's length, and the determinant's magnitude.
                const float area = ((positions[b] - positions[a]) ^ (positions[c] - positions[a])).length();
                const osg::Vec2f firstEdge = coords[b] - coords[a];
                const osg::Vec2f secondEdge = coords[c] - coords[a];
                const float uvArea = std::abs(firstEdge.x() * secondEdge.y() - secondEdge.x() * firstEdge.y());
                if (!(area > 0.0f) || !(uvArea > 0.0f))
                    return 0.0f;

                sparsest = std::min(sparsest, uvArea / area);
            }
            return sparsest;
        }
    }

    UvDensity uvDensityOf(const std::span<const osg::Vec3f> positions, const std::span<const osg::Vec2f> first,
        const std::span<const osg::Vec2f> second, const std::span<const std::uint32_t> indices)
    {
        assert(indices.size() % 3 == 0 && "indices that are not whole triangles");
        assert((first.empty() || first.size() == positions.size()) && "a first set apart from the positions");
        assert((second.empty() || second.size() == positions.size()) && "a second set apart from the positions");
        assert(std::ranges::all_of(indices, [&](const std::uint32_t at) { return at < positions.size(); })
            && "an index past the positions");

        return UvDensity{ .mFirst = sparsestOf(positions, first, indices),
            .mSecond = sparsestOf(positions, second, indices) };
    }
}
