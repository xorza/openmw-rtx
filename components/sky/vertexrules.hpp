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

    /// The alpha Morrowind's atmosphere paints vertex `index` with: the mesh is a cylinder that lists
    /// its two rings alternately, and every second vertex stands in the bottom ring, which the sky
    /// colour fades out to. The file records none of it. Both renderers read it here, the
    /// rasterizer's `ModVertexAlphaVisitor::Atmosphere` and the ray tracer's `readAtmosphere`.
    inline float atmosphereAlphaOf(const std::size_t index)
    {
        return index % 2 != 0 ? 0.f : 1.f;
    }

    /// Whether the star dome draws a vertex authored in `colour`: exactly white, and nothing else,
    /// which leaves its bottom ring out. `ModVertexAlphaVisitor::Stars` and the ray tracer's night
    /// sky both read it here.
    inline bool starVertexShown(const osg::Vec4f& colour)
    {
        return colour.x() == 1.f;
    }
}
