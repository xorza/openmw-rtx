#include "tracedground.hpp"

#include <osg/Vec3f>

#include <apps/openmw/mwworld/cell.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm/position.hpp>
#include <components/esm/util.hpp>
#include <components/rtx/mirror/cells/cellring.hpp>
#include <components/rtx/mirror/cells/prepared.hpp>
#include <components/terrain/pagedcellref.hpp>

namespace MWRender
{
    bool standsAway(const bool sameWorldspace, const ESM::Position& standing, const ESM::Position& placed,
        const ESM::RefId worldspace)
    {
        if (!sameWorldspace)
            return true;
        const ESM::ExteriorCellLocation now
            = ESM::positionToExteriorCellLocation(standing.pos[0], standing.pos[1], worldspace);
        const ESM::ExteriorCellLocation content
            = ESM::positionToExteriorCellLocation(placed.pos[0], placed.pos[1], worldspace);
        return now.mX != content.mX || now.mY != content.mY;
    }

    TracedGround::TracedGround(
        const GroundSpec& spec, Resource::SceneManager& scenes, const unsigned int nodeMask, Rtx::CellRing& ring)
        : mTerrain(spec.mSceneRoot, spec.mWorldRoot, spec.mStorage, scenes, *this, nodeMask, spec.mWorldspace)
        , mRing(ring)
    {
    }

    bool TracedGround::standsGround(const osg::Vec2i& cell) const
    {
        return mRing.standsGround(cell);
    }

    std::span<const Rtx::Placement> TracedGround::placementsIn(const osg::Vec2i& cell) const
    {
        return mRing.placementsIn(cell);
    }

    bool TracedGround::enableReference(int type, const MWWorld::ConstPtr& ptr, const bool enabled)
    {
        mRing.setReferenceEnabled(ptr.getCellRef().getRefNum(), enabled);
        return false;
    }

    void TracedGround::blacklistReference(int type, const MWWorld::ConstPtr& paged, const MWWorld::ConstPtr& now)
    {
        const ESM::RefNum refnum = now.getCellRef().getRefNum();
        if (now.getCellRef().getCount() == 0)
        {
            mRing.blacklistReference(refnum);
            return;
        }

        // Read after the game wrote it: a move, a turn and a scale each write the reference's own
        // data before they tell the paging. The worldspace is the one the distance found it in.
        const ESM::Position& position = now.getRefData().getPosition();
        const ESM::RefId worldspace = paged.getCell()->getCell()->getWorldSpace();
        const bool sameWorldspace
            = now.isInCell() && now.getCell()->isExterior() && now.getCell()->getCell()->getWorldSpace() == worldspace;
        mRing.moveReference(refnum,
            Rtx::ReferencePlace{ .mPosition = position.asVec3(),
                .mRotation = osg::Vec3f(position.rot[0], position.rot[1], position.rot[2]),
                .mScale = now.getCellRef().getScale() },
            standsAway(sameWorldspace, position, now.getCellRef().getPosition(), worldspace));
    }

    void TracedGround::setGate(const std::uint32_t gate, const Terrain::GateState state)
    {
        mRing.setGate(gate, state);
    }

    void TracedGround::clear()
    {
        mRing.forgetReferences();
    }
}
