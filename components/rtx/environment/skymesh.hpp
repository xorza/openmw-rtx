#pragma once

#include <vector>

#include <osg/Node>
#include <osg/Vec3f>

namespace osg
{
    class Geometry;
}

namespace Rtx
{
    /// Appends each vertex of `geometry` where the graph puts it: through every transform on
    /// `path`, the node path a visitor reached it by. The one placement the sky's three readers
    /// take a mesh by, because Morrowind's meshes hang their shapes under transforms: the cloud
    /// cap's `NiTriShape` sits fifteen units below its `NiNode`, and the atmosphere hangs upside
    /// down under a root rotation of `diag(1, -1, -1)`.
    ///
    /// @return false where `geometry` holds no `osg::Vec3Array`, having appended nothing.
    bool placedVertices(const osg::Geometry& geometry, const osg::NodePath& path, std::vector<osg::Vec3f>& into);
}
