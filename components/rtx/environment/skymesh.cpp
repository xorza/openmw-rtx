#include "skymesh.hpp"

#include <vector>

#include <osg/Array>
#include <osg/Geometry>
#include <osg/Matrixf>
#include <osg/Transform>
#include <osg/Vec3f>

namespace Rtx
{
    bool placedVertices(const osg::Geometry& geometry, const osg::NodePath& path, std::vector<osg::Vec3f>& into)
    {
        const auto* vertices = dynamic_cast<const osg::Vec3Array*>(geometry.getVertexArray());
        if (vertices == nullptr)
            return false;

        const osg::Matrixf placed = osg::computeLocalToWorld(path);
        into.reserve(into.size() + vertices->size());
        for (const osg::Vec3f& vertex : *vertices)
            into.push_back(placed.preMult(vertex));
        return true;
    }
}
