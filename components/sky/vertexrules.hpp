#pragma once

#include <cstddef>

#include <osg/Vec4f>

namespace Sky
{
    /// Which row of Morrowind's cloud shell vertex `index` stands in, by the order the shell's mesh
    /// lists them: the engine fades a deck out toward the horizon by row, and the file records
    /// nothing of it. **Both renderers read the rows here**, the rasterizer's
    /// `ModVertexAlphaVisitor::Clouds` and the ray tracer's cloud shell; each keeps its own alpha
    /// for the faded row, because the rasterizer writes `0.25098` and the trace reads the byte
    /// sixty-four exactly, and neither picture may move for the other.
    enum class CloudRow
    {
        Bottom,
        Second,
        Upper,
    };

    inline CloudRow cloudRowOf(const std::size_t index)
    {
        if (index >= 49 && index <= 64)
            return CloudRow::Bottom;
        if (index >= 33 && index <= 48)
            return CloudRow::Second;
        return CloudRow::Upper;
    }

    /// Whether the star dome draws a vertex authored in `colour`: exactly white, and nothing else,
    /// which leaves its bottom ring out. `ModVertexAlphaVisitor::Stars` and the ray tracer's night
    /// sky both read it here.
    inline bool starVertexShown(const osg::Vec4f& colour)
    {
        return colour.x() == 1.f;
    }
}
