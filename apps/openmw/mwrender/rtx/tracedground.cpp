#include "tracedground.hpp"

#include <osg/Vec3f>

#include <apps/openmw/mwworld/cell.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm/position.hpp>
#include <components/esm/util.hpp>
#include <components/rtx/mirror/cells/prepared.hpp>
#include <components/terrain/pagedcellref.hpp>

#include "worldmirror.hpp"

namespace MWRender
{
    TracedGround::TracedGround(
        const GroundSpec& spec, Resource::SceneManager& scenes, const unsigned int nodeMask, WorldMirror& mirror)
        : mTerrain(spec.mSceneRoot, spec.mWorldRoot, spec.mStorage, scenes, *this, nodeMask, spec.mWorldspace)
        , mMirror(mirror)
    {
    }

    bool TracedGround::standsGround(const osg::Vec2i& cell) const
    {
        return mMirror.getRing().standsGround(cell);
    }

    std::span<const Rtx::Placement> TracedGround::placementsIn(const osg::Vec2i& cell) const
    {
        return mMirror.getRing().placementsIn(cell);
    }

    bool TracedGround::enableReference(int type, const MWWorld::ConstPtr& ptr, const bool enabled)
    {
        mMirror.getRing().setReferenceEnabled(ptr.getCellRef().getRefNum(), enabled);
        return false;
    }

    void TracedGround::blacklistReference(int type, const MWWorld::ConstPtr& ptr)
    {
        const ESM::RefNum refnum = ptr.getCellRef().getRefNum();
        if (ptr.getCellRef().getCount() == 0)
        {
            mMirror.getRing().blacklistReference(refnum);
            return;
        }

        // Read after the game wrote it: a move, a turn and a scale each write the reference's own
        // data before they tell the paging. Away where the cell it stands in now is not the one its
        // content position stands in, which the reference keeps (`CellRef::getPosition`): a cell's
        // own reference may stand past its edge, and a lantern swung there has gone nowhere.
        const ESM::Position& position = ptr.getRefData().getPosition();
        const ESM::Position& placed = ptr.getCellRef().getPosition();
        const ESM::RefId worldspace = ptr.getCell()->getCell()->getWorldSpace();
        const ESM::ExteriorCellLocation now
            = ESM::positionToExteriorCellLocation(position.pos[0], position.pos[1], worldspace);
        const ESM::ExteriorCellLocation content
            = ESM::positionToExteriorCellLocation(placed.pos[0], placed.pos[1], worldspace);
        mMirror.getRing().moveReference(refnum,
            Rtx::ReferencePlace{ .mPosition = position.asVec3(),
                .mRotation = osg::Vec3f(position.rot[0], position.rot[1], position.rot[2]),
                .mScale = ptr.getCellRef().getScale() },
            now.mX != content.mX || now.mY != content.mY);
    }

    void TracedGround::setGate(const std::uint32_t gate, const Terrain::GateState state)
    {
        mMirror.getRing().setGate(gate, state);
    }

    void TracedGround::clear()
    {
        mMirror.getRing().forgetReferences();
    }
}
