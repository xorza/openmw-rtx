#include "static.hpp"

#include <apps/openmw/mwphysics/physicssystem.hpp>
#include <apps/openmw/mwrender/objects.hpp>
#include <apps/openmw/mwrender/renderinginterface.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "classmodel.hpp"

namespace MWClass
{
    Static::Static()
        : MWWorld::RegisteredClass<Static>(ESM::Static::sRecordId)
    {
    }

    void Static::insertObjectRendering(
        const MWWorld::Ptr& ptr, const std::string& model, MWRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
            ptr.getRefData().getBaseNode()->setNodeMask(MWRender::Mask_Static);
        }
    }

    void Static::insertObject(const MWWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        insertObjectPhysics(ptr, model, rotation, physics);
    }

    void Static::insertObjectPhysics(const MWWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        physics.addObject(ptr, VFS::Path::toNormalized(model), rotation, MWPhysics::CollisionType_World);
    }

    VFS::Path::NormalizedView Static::getModel(const MWWorld::ConstPtr& ptr) const
    {
        return getClassModel<ESM::Static>(ptr);
    }

    std::string_view Static::getName(const MWWorld::ConstPtr& ptr) const
    {
        return {};
    }

    bool Static::hasToolTip(const MWWorld::ConstPtr& ptr) const
    {
        return false;
    }

    MWWorld::Ptr Static::copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const
    {
        const MWWorld::LiveCellRef<ESM::Static>* ref = ptr.get<ESM::Static>();

        return MWWorld::Ptr(cell.insert(ref), &cell);
    }
}
