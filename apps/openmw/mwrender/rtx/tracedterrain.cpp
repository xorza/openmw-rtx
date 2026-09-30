#include "tracedterrain.hpp"

#include <algorithm>
#include <utility>

#include <osg/Geometry>
#include <osg/PositionAttitudeTransform>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/terrain/storage.hpp>
#include <components/terrain/view.hpp>

namespace MWRender
{
    namespace
    {
        class NullView final : public Terrain::View
        {
        public:
            void reset() override {}
        };
    }

    TracedTerrain::TracedTerrain(
        osg::Group& sceneRoot, Terrain::Storage& storage, const unsigned int nodeMask, const ESM::RefId worldspace)
        : Terrain::World(&sceneRoot, &storage, nodeMask, worldspace)
        , mNormals(new osg::Vec3Array)
        , mColours(new osg::Vec4ubArray)
    {
    }

    TracedTerrain::~TracedTerrain() = default;

    Terrain::View* TracedTerrain::createView()
    {
        return new NullView;
    }

    TracedTerrain::CellGrid TracedTerrain::takeGrid()
    {
        if (!mSpare.empty())
        {
            CellGrid grid = std::move(mSpare.back());
            mSpare.pop_back();
            return grid;
        }

        CellGrid grid{
            .mRoot = new osg::PositionAttitudeTransform,
            .mGeometry = new osg::Geometry,
            .mPositions = new osg::Vec3Array,
        };

        // The triangles `TerrainGrid` draws a cell with, and no stitching flags: the vertex order
        // is `fillVertexBuffers`' own, and the diamond splits each quad the way the game does, so
        // a slope is cut here as it is cut on the screen.
        const auto verts = static_cast<unsigned int>(mStorage->getCellVertices(mWorldspace));
        grid.mGeometry->setUseDisplayList(false);
        grid.mGeometry->setVertexArray(grid.mPositions);
        grid.mGeometry->addPrimitiveSet(mBuffers.getIndexBuffer(verts, 0));
        grid.mRoot->addChild(grid.mGeometry);

        return grid;
    }

    TracedTerrain::CellGrid* TracedTerrain::findGrid(const osg::Vec2i& cell)
    {
        const auto found
            = std::find_if(mCells.begin(), mCells.end(), [&](const CellGrid& grid) { return grid.mCell == cell; });
        return found == mCells.end() ? nullptr : &*found;
    }

    void TracedTerrain::loadCell(const int x, const int y)
    {
        const osg::Vec2i cell(x, y);
        if (findGrid(cell) != nullptr)
            return;

        Terrain::World::loadCell(x, y);

        // The whole cell at full detail, as the ring reads it: positions about the cell's middle,
        // which is where the transform stands them. A cell with no land record is the default
        // plane, which is the ground the rasterizer's chunk stands there too.
        CellGrid grid = takeGrid();
        grid.mCell = cell;
        const osg::Vec2f centre(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
        mStorage->fillVertexBuffers(0, 1.0f, centre, mWorldspace, *grid.mPositions, *mNormals, *mColours);
        grid.mPositions->dirty();
        grid.mGeometry->dirtyBound();

        const float cellSize = mStorage->getCellWorldSize(mWorldspace);
        grid.mRoot->setPosition(osg::Vec3f(centre.x() * cellSize, centre.y() * cellSize, 0.0f));

        mTerrainRoot->addChild(grid.mRoot);
        mCells.push_back(std::move(grid));
    }

    void TracedTerrain::unloadCell(const int x, const int y)
    {
        CellGrid* const found = findGrid(osg::Vec2i(x, y));
        if (found == nullptr)
            return;

        Terrain::World::unloadCell(x, y);

        mTerrainRoot->removeChild(found->mRoot);
        mSpare.push_back(std::move(*found));
        if (found != &mCells.back())
            *found = std::move(mCells.back());
        mCells.pop_back();
    }
}
