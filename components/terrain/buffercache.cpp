#include "buffercache.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <osg/PrimitiveSet>

#include "defs.hpp"

namespace
{

    template <typename IndexArrayType>
    osg::ref_ptr<IndexArrayType> createIndexBuffer(std::uint32_t flags, std::uint32_t verts)
    {
        using IndexType = IndexArrayType::value_type;
        // LOD level n means every 2^n-th vertex is kept, but we currently handle LOD elsewhere.
        std::size_t lodLevel = 0; //(flags >> (4*4));

        std::size_t lodDeltas[4];
        for (std::int32_t i = 0; i < 4; ++i)
            lodDeltas[i] = (flags >> (4 * i)) & (0xf);

        bool anyDeltas = (lodDeltas[Terrain::North] || lodDeltas[Terrain::South] || lodDeltas[Terrain::West]
            || lodDeltas[Terrain::East]);

        std::size_t increment = std::size_t{ 1 } << lodLevel;
        assert(increment < verts);

        osg::ref_ptr<IndexArrayType> indices(new IndexArrayType(osg::PrimitiveSet::TRIANGLES));
        indices->reserve(static_cast<std::size_t>((verts - 1) * (verts - 1) * 2) * 3 / increment);

        std::size_t rowStart = 0, colStart = 0, rowEnd = verts - 1, colEnd = verts - 1;
        // If any edge needs stitching we'll skip all edges at this point,
        // mainly because stitching one edge would have an effect on corners and on the adjacent edges
        if (anyDeltas)
        {
            colStart += increment;
            colEnd -= increment;
            rowEnd -= increment;
            rowStart += increment;
        }
        for (std::size_t row = rowStart; row < rowEnd; row += increment)
        {
            for (std::size_t col = colStart; col < colEnd; col += increment)
            {
                // diamond pattern
                if ((row + col % 2) % 2 == 1)
                {
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row + increment));
                    indices->push_back(static_cast<IndexType>(verts * col + row + increment));

                    indices->push_back(static_cast<IndexType>(verts * col + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row));
                    indices->push_back(static_cast<IndexType>(verts * (col) + row + increment));
                }
                else
                {
                    indices->push_back(static_cast<IndexType>(verts * col + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row + increment));
                    indices->push_back(static_cast<IndexType>(verts * col + row + increment));

                    indices->push_back(static_cast<IndexType>(verts * col + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + increment) + row + increment));
                }
            }
        }

        std::size_t innerStep = increment;
        if (anyDeltas)
        {
            // Now configure LOD transitions at the edges - this is pretty tedious,
            // and some very long and boring code, but it works great

            // South
            std::size_t row = 0;
            std::size_t outerStep = std::size_t{ 1 } << (lodDeltas[Terrain::South] + lodLevel);
            for (std::size_t col = 0; col < verts - 1; col += outerStep)
            {
                indices->push_back(static_cast<IndexType>(verts * col + row));
                indices->push_back(static_cast<IndexType>(verts * (col + outerStep) + row));
                // Make sure not to touch the right edge
                if (col + outerStep == verts - 1)
                    indices->push_back(static_cast<IndexType>(verts * (col + outerStep - innerStep) + row + innerStep));
                else
                    indices->push_back(static_cast<IndexType>(verts * (col + outerStep) + row + innerStep));

                for (std::size_t i = 0; i < outerStep; i += innerStep)
                {
                    // Make sure not to touch the left or right edges
                    if (col + i == 0 || col + i == verts - 1 - innerStep)
                        continue;
                    indices->push_back(static_cast<IndexType>(verts * (col) + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + i + innerStep) + row + innerStep));
                    indices->push_back(static_cast<IndexType>(verts * (col + i) + row + innerStep));
                }
            }

            // North
            row = verts - 1;
            outerStep = std::size_t{ 1 } << (lodDeltas[Terrain::North] + lodLevel);
            for (std::size_t col = 0; col < verts - 1; col += outerStep)
            {
                indices->push_back(static_cast<IndexType>(verts * (col + outerStep) + row));
                indices->push_back(static_cast<IndexType>(verts * col + row));
                // Make sure not to touch the left edge
                if (col == 0)
                    indices->push_back(static_cast<IndexType>(verts * (col + innerStep) + row - innerStep));
                else
                    indices->push_back(static_cast<IndexType>(verts * col + row - innerStep));

                for (std::size_t i = 0; i < outerStep; i += innerStep)
                {
                    // Make sure not to touch the left or right edges
                    if (col + i == 0 || col + i == verts - 1 - innerStep)
                        continue;
                    indices->push_back(static_cast<IndexType>(verts * (col + i) + row - innerStep));
                    indices->push_back(static_cast<IndexType>(verts * (col + i + innerStep) + row - innerStep));
                    indices->push_back(static_cast<IndexType>(verts * (col + outerStep) + row));
                }
            }

            // West
            std::size_t col = 0;
            outerStep = std::size_t{ 1 } << (lodDeltas[Terrain::West] + lodLevel);
            for (row = 0; row < verts - 1; row += outerStep)
            {
                indices->push_back(static_cast<IndexType>(verts * col + row + outerStep));
                indices->push_back(static_cast<IndexType>(verts * col + row));
                // Make sure not to touch the top edge
                if (row + outerStep == verts - 1)
                    indices->push_back(static_cast<IndexType>(verts * (col + innerStep) + row + outerStep - innerStep));
                else
                    indices->push_back(static_cast<IndexType>(verts * (col + innerStep) + row + outerStep));

                for (std::size_t i = 0; i < outerStep; i += innerStep)
                {
                    // Make sure not to touch the top or bottom edges
                    if (row + i == 0 || row + i == verts - 1 - innerStep)
                        continue;
                    indices->push_back(static_cast<IndexType>(verts * col + row));
                    indices->push_back(static_cast<IndexType>(verts * (col + innerStep) + row + i));
                    indices->push_back(static_cast<IndexType>(verts * (col + innerStep) + row + i + innerStep));
                }
            }

            // East
            col = verts - 1;
            outerStep = std::size_t{ 1 } << (lodDeltas[Terrain::East] + lodLevel);
            for (row = 0; row < verts - 1; row += outerStep)
            {
                indices->push_back(static_cast<IndexType>(verts * col + row));
                indices->push_back(static_cast<IndexType>(verts * col + row + outerStep));
                // Make sure not to touch the bottom edge
                if (row == 0)
                    indices->push_back(static_cast<IndexType>(verts * (col - innerStep) + row + innerStep));
                else
                    indices->push_back(static_cast<IndexType>(verts * (col - innerStep) + row));

                for (std::size_t i = 0; i < outerStep; i += innerStep)
                {
                    // Make sure not to touch the top or bottom edges
                    if (row + i == 0 || row + i == verts - 1 - innerStep)
                        continue;
                    indices->push_back(static_cast<IndexType>(verts * col + row + outerStep));
                    indices->push_back(static_cast<IndexType>(verts * (col - innerStep) + row + i + innerStep));
                    indices->push_back(static_cast<IndexType>(verts * (col - innerStep) + row + i));
                }
            }
        }

        return indices;
    }

}

namespace Terrain
{

    osg::ref_ptr<osg::Vec2Array> BufferCache::getUVBuffer(std::uint32_t numVerts)
    {
        std::scoped_lock lock(mUvBufferMutex);
        if (mUvBufferMap.contains(static_cast<std::int32_t>(numVerts)))
        {
            return mUvBufferMap[static_cast<std::int32_t>(numVerts)];
        }

        auto vertexCount = static_cast<std::int32_t>(numVerts * numVerts);

        osg::ref_ptr<osg::Vec2Array> uvs(new osg::Vec2Array(osg::Array::BIND_PER_VERTEX));
        uvs->reserve(static_cast<std::size_t>(vertexCount));

        for (std::uint32_t col = 0; col < numVerts; ++col)
        {
            for (std::uint32_t row = 0; row < numVerts; ++row)
            {
                const float u = static_cast<float>(col) / static_cast<float>(numVerts - 1);
                const float v = static_cast<float>(row) / static_cast<float>(numVerts - 1);
                uvs->push_back(osg::Vec2f(u, v));
            }
        }

        // Assign a VBO here to enable state sharing between different Geometries.
        uvs->setVertexBufferObject(new osg::VertexBufferObject);

        mUvBufferMap[static_cast<std::int32_t>(numVerts)] = uvs;
        return uvs;
    }

    osg::ref_ptr<osg::DrawElements> BufferCache::getIndexBuffer(std::uint32_t numVerts, std::uint32_t flags)
    {
        std::pair<std::int32_t, std::int32_t> id = std::make_pair(numVerts, flags);
        std::scoped_lock lock(mIndexBufferMutex);

        if (mIndexBufferMap.contains(id))
        {
            return mIndexBufferMap[id];
        }

        osg::ref_ptr<osg::DrawElements> buffer;

        if (numVerts * numVerts <= (0xffffu))
            buffer = createIndexBuffer<osg::DrawElementsUShort>(flags, numVerts);
        else
            buffer = createIndexBuffer<osg::DrawElementsUInt>(flags, numVerts);

        // Assign a EBO here to enable state sharing between different Geometries.
        buffer->setElementBufferObject(new osg::ElementBufferObject);

        mIndexBufferMap[id] = buffer;
        return buffer;
    }

    void BufferCache::clearCache()
    {
        {
            std::scoped_lock lock(mIndexBufferMutex);
            mIndexBufferMap.clear();
        }
        {
            std::scoped_lock lock(mUvBufferMutex);
            mUvBufferMap.clear();
        }
    }

    void BufferCache::releaseGLObjects(osg::State* state)
    {
        {
            std::scoped_lock lock(mIndexBufferMutex);
            for (const auto& [_, indexbuffer] : mIndexBufferMap)
                indexbuffer->releaseGLObjects(state);
        }
        {
            std::scoped_lock lock(mUvBufferMutex);
            for (const auto& [_, uvbuffer] : mUvBufferMap)
                uvbuffer->releaseGLObjects(state);
        }
    }

}
