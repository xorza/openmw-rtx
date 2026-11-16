#pragma once

#include <MyGUI_TextIterator.h>

#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/registeredclass.hpp>

namespace MWClass
{
    class Static : public MWWorld::RegisteredClass<Static>
    {
        friend MWWorld::RegisteredClass<Static>;

        Static();

        MWWorld::Ptr copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const override;

    public:
        void insertObjectRendering(const MWWorld::Ptr& ptr, const std::string& model,
            MWRender::RenderingInterface& renderingInterface) const override;
        ///< Add reference into a cell for rendering

        void insertObject(const MWWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
            MWPhysics::PhysicsSystem& physics) const override;
        void insertObjectPhysics(const MWWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
            MWPhysics::PhysicsSystem& physics) const override;

        std::string_view getName(const MWWorld::ConstPtr& ptr) const override;
        ///< \return name or ID; can return an empty string.

        bool hasToolTip(const MWWorld::ConstPtr& ptr) const override;
        ///< @return true if this object has a tooltip when focused (default implementation: true)

        VFS::Path::NormalizedView getModel(const MWWorld::ConstPtr& ptr) const override;
    };
}
