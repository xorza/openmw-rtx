#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include <osg/Vec3f>

namespace Rtx
{
    /// The bits of a run of points, mixed to a hash: a triangle's three corners, an edge's two ends,
    /// or one position. Mixed here rather than through `Misc::hashCombine`, which reaches
    /// `std::hash<float>` — a byte-wise murmur over four bytes — and eighteen of those per triangle
    /// cost more than the fold around them, where FNV over the bits and one final mix cost nine
    /// multiplies. A zero is normalised first, because -0 and 0 compare equal, and a hash that told
    /// the two apart would never find the point it was looking for.
    inline std::size_t hashPoints(std::span<const osg::Vec3f> points)
    {
        std::uint64_t seed = 0xcbf29ce484222325ull;
        for (const osg::Vec3f& point : points)
            for (const float value : { point.x(), point.y(), point.z() })
            {
                seed ^= std::bit_cast<std::uint32_t>(value == 0.0f ? 0.0f : value);
                seed *= 0x100000001b3ull;
            }

        // FNV moves its high bits far more than its low ones, and the tables mask the low ones.
        seed ^= seed >> 29;
        seed *= 0xbf58476d1ce4e5b9ull;
        seed ^= seed >> 32;

        return static_cast<std::size_t>(seed);
    }
}
