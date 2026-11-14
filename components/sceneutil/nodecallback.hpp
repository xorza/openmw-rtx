#pragma once

#include <osg/Callback>
#include <osg/Node>
#include <osg/NodeVisitor>

namespace SceneUtil
{

    template <class Derived, typename NodeType = osg::Node*, typename VisitorType = osg::NodeVisitor*>
    class NodeCallback : public osg::Callback
    {
        friend Derived;

        NodeCallback() {}
        NodeCallback(const NodeCallback& nc, const osg::CopyOp& copyop)
            : osg::Callback(nc, copyop)
        {
        }

    public:
        bool run(osg::Object* object, osg::Object* data) override
        {
            static_cast<Derived*>(this)->operator()(
                static_cast<NodeType>(object), static_cast<VisitorType>(data->asNodeVisitor()));
            return true;
        }

        template <typename VT>
        void traverse(NodeType object, VT data)
        {
            if (_nestedCallback.valid())
                _nestedCallback->run(object, data);
            else
                data->traverse(*object);
        }
    };

}
