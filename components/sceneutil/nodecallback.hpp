#ifndef SCENEUTIL_NODECALLBACK_H
#define SCENEUTIL_NODECALLBACK_H

#include <osg/Callback>

#include <components/crashcatcher/crash.hpp>

namespace osg
{
    class Node;
    class NodeVisitor;
}

namespace SceneUtil
{

    template <class Derived, typename NodeType = osg::Node*, typename VisitorType = osg::NodeVisitor*>
    class NodeCallback : public osg::Callback
    {
    public:
        NodeCallback() {}
        NodeCallback(const NodeCallback& nc, const osg::CopyOp& copyop)
            : osg::Callback(nc, copyop)
        {
        }

        bool run(osg::Object* object, osg::Object* data) override
        {
            // OSG runs a node callback with the visitor that reached the node and nothing else.
            osg::NodeVisitor* visitor = Crash::notNull(data->asNodeVisitor(), "a node callback run by no visitor");
            static_cast<Derived*>(this)->operator()((NodeType)object, (VisitorType)visitor);
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
#endif
