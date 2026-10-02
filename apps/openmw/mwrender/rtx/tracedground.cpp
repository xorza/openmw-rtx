#include "tracedground.hpp"

#include <components/terrain/pagedcellref.hpp>

#include "../../mwworld/ptr.hpp"
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

    bool TracedGround::enableReference(int type, const MWWorld::ConstPtr& ptr, const bool enabled)
    {
        mMirror.getRing().setReferenceEnabled(ptr.getCellRef().getRefNum(), enabled);
        return false;
    }

    void TracedGround::blacklistReference(int type, const MWWorld::ConstPtr& ptr)
    {
        mMirror.getRing().blacklistReference(ptr.getCellRef().getRefNum());
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
