#pragma once

#include <cstdint>

#include <components/esm/position.hpp>
#include <components/esm/refid.hpp>

#include <apps/openmw/mwrender/ground.hpp>

#include "tracedterrain.hpp"

namespace Rtx
{
    class CellRing;
}

namespace Resource
{
    class SceneManager;
}

namespace MWRender
{
    /// **Whether a reference moved to `standing` has gone away from the cell its content places it
    /// in, `placed`**, compared as cells on `worldspace`'s grid: the paging's own reference may stand
    /// past its cell's edge, and a lantern swung there has gone nowhere. One standing in an interior
    /// or another worldspace, which `sameWorldspace` says it does not, is away whatever its
    /// coordinates read as on this grid.
    bool standsAway(
        bool sameWorldspace, const ESM::Position& standing, const ESM::Position& placed, ESM::RefId worldspace);

    /// The ground and the distance as the ray tracer has them: a `TracedTerrain` that draws no
    /// chunks, and the mirror's cell ring, which stands the distant statics itself. What the game
    /// says of a reference reaches the ring through this and the mirror, the way it reaches the
    /// paging through `GlGround`.
    class TracedGround final : public Ground, public StandingGround
    {
    public:
        TracedGround(
            const GroundSpec& spec, Resource::SceneManager& scenes, unsigned int nodeMask, Rtx::CellRing& ring);

        Terrain::World& getTerrain() override { return mTerrain; }

        /// Told to the ring by its reference number, and false: the ring stands and drops on its
        /// own walk, so the game has nothing to rebuild here. The rest of what the reference says
        /// is the paging's, and the seam's default answers the two questions a ring has no cache
        /// and no grid for.
        bool enableReference(int type, const MWWorld::ConstPtr& ptr, bool enabled) override;

        /// Where the game holds the reference now, to the ring (`CellPlacer::moveReference`), or
        /// that it is gone, where the game deleted it.
        void blacklistReference(int type, const MWWorld::ConstPtr& paged, const MWWorld::ConstPtr& now) override;
        void setGate(std::uint32_t gate, Terrain::GateState state) override;

        void clear() override;

        /// The ring's word: whether the trace draws the cell's ground.
        bool standsGround(const osg::Vec2i& cell) const override;

        std::span<const Rtx::Placement> placementsIn(const osg::Vec2i& cell) const override;

    private:
        TracedTerrain mTerrain;

        /// Borrowed: the renderer that made this owns the mirror the ring stands in, and outlives it.
        Rtx::CellRing& mRing;
    };
}
