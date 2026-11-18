#pragma once

#include <osg/Quat>

#include <components/sceneutil/nodecallback.hpp>

namespace osg
{
    class MatrixTransform;
}

namespace MWRender
{

    /// Applies a rotation in \a relativeTo's space.
    /// @note Assumes that the node being rotated has its "original" orientation set every frame by a different
    /// controller. The rotation is then applied on top of that orientation.
    class RotateController : public SceneUtil::NodeCallback<RotateController, osg::MatrixTransform*>
    {
    public:
        explicit RotateController(osg::Node* relativeTo);

        void setEnabled(bool enabled);
        void setOffset(const osg::Vec3f& offset);
        void setRotate(const osg::Quat& rotate);

        const osg::Vec3f& getOffset() const { return mOffset; }

        const osg::Quat& getRotate() const { return mRotate; }

        osg::Node* getRelativeTo() const { return mRelativeTo; }

        void operator()(osg::MatrixTransform* node, osg::NodeVisitor* nv);

    protected:
        bool mEnabled;
        osg::Vec3f mOffset;
        osg::Quat mRotate;
        osg::Node* mRelativeTo;
    };

}
