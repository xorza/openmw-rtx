#pragma once

#include <cstdint>
#include <map>

#include <osg/Vec2f>

#include "world.hpp"

namespace osg
{
    class Group;
    class Stats;
}

namespace Resource
{
    class ResourceSystem;
}

namespace Terrain
{
    class Storage;

    /// @brief Simple terrain implementation that loads cells in a grid, with no LOD. Only requested cells are loaded.
    class TerrainGrid : public Terrain::World
    {
    public:
        explicit TerrainGrid(osg::Group* parent, osg::Group* compileRoot, Resource::ResourceSystem* resourceSystem,
            Storage* storage, std::uint32_t nodeMask, ESM::RefId worldspace, double expiryDelay,
            std::uint32_t preCompileMask = ~0u, std::uint32_t borderMask = 0);
        TerrainGrid(osg::Group* parent, Storage* storage, ESM::RefId worldspace, std::uint32_t nodeMask = ~0u);
        ~TerrainGrid() override;

        void cacheCell(View* view, std::int32_t x, std::int32_t y) override;

        /// @note Not thread safe.
        void loadCell(std::int32_t x, std::int32_t y) override;

        /// @note Not thread safe.
        void unloadCell(std::int32_t x, std::int32_t y) override;

        std::unique_ptr<View> createView() override;

    protected:
        bool isGridEmpty() const { return mGrid.empty(); }

    private:
        osg::ref_ptr<osg::Node> buildTerrain(osg::Group* parent, float chunkSize, const osg::Vec2f& chunkCenter);
        void updateWaterCulling();

        // split each ESM::Cell into mNumSplits*mNumSplits terrain chunks
        std::uint32_t mNumSplits;

        CellBorder::CellGrid mGrid;
    };
}
