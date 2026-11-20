#include "tracedground.hpp"

#include <cstdint>

#include <osg/Vec3f>

#include <apps/openmw/mwworld/cell.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm/defs.hpp>
#include <components/esm/position.hpp>
#include <components/esm/util.hpp>
#include <components/rtx/mirror/cells/cellstreamer.hpp>
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
        const GroundSpec& spec, Resource::SceneManager& scenes, const std::uint32_t nodeMask, Rtx::CellStreamer& ring)
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

    bool TracedGround::enableReference(std::int32_t type, const MWWorld::ConstPtr& ptr, const bool enabled)
    {
        mRing.setReferenceEnabled(ptr.getCellRef().getRefNum(), enabled);
        return false;
    }

    bool ringMayHold(const std::int32_t type)
    {
        switch (type)
        {
            case ESM::REC_NPC_:
            case ESM::REC_CREA:
            case ESM::REC_LEVC:
            case ESM::REC_NPC_4:
            case ESM::REC_CREA4:
            case ESM::REC_LVLC4:
            case ESM::REC_LVLN4:
                return false;
            default:
                return true;
        }
    }

    void TracedGround::blacklistReference(
        std::int32_t type, const MWWorld::ConstPtr& paged, const MWWorld::ConstPtr& now)
    {
        if (!ringMayHold(type))
            return;

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
            standsAway(sameWorldspace, position, now.getCellRef().getPosition(), worldspace)
                ? Rtx::ReferenceStands::Away
                : Rtx::ReferenceStands::InItsCell);
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
