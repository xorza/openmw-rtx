#include "groundcovershapes.hpp"

#include <osg/Array>
#include <osg/Geometry>
#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <osg/Transform>

namespace MWRender
{
    namespace
    {
        /// Whether any transform under the model is one a bake cannot stand in for: one with a
        /// callback, which a controller or a billboard writes its matrix through, or one that is no
        /// plain matrix, whose identity nothing here sets.
        class FixedTransforms : public osg::NodeVisitor
        {
        public:
            FixedTransforms()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }

            void apply(osg::Transform& transform) override
            {
                mBakeable = mBakeable && transform.asMatrixTransform() != nullptr
                    && transform.getUpdateCallback() == nullptr && transform.getCullCallback() == nullptr;
                traverse(transform);
            }

            bool mBakeable = true;
        };

        /// Bakes the path's transforms into each geometry on the way down, and sets each transform
        /// to the identity on the way back up, once every geometry under it has read it.
        class Baker : public osg::NodeVisitor
        {
        public:
            Baker()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
            }

            void apply(osg::Geometry& geometry) override
            {
                const osg::Matrix placed = osg::computeLocalToWorld(getNodePath());
                if (placed.isIdentity())
                    return;

                if (auto* const vertices = dynamic_cast<osg::Vec3Array*>(geometry.getVertexArray()))
                {
                    for (osg::Vec3f& vertex : *vertices)
                        vertex = vertex * placed;
                    vertices->dirty();
                }

                // By the inverse transpose, which keeps a normal square to its surface under a scale
                // that is not the same on every axis.
                if (auto* const normals = dynamic_cast<osg::Vec3Array*>(geometry.getNormalArray()))
                {
                    const osg::Matrix inverse = osg::Matrix::inverse(placed);
                    for (osg::Vec3f& normal : *normals)
                    {
                        normal = osg::Matrix::transform3x3(inverse, normal);
                        normal.normalize();
                    }
                    normals->dirty();
                }

                geometry.dirtyBound();
            }

            void apply(osg::MatrixTransform& transform) override
            {
                traverse(transform);
                transform.setMatrix(osg::Matrix::identity());
            }
        };
    }

    bool GroundcoverShapes::bakeTransforms(osg::Node& model)
    {
        FixedTransforms fixed;
        model.accept(fixed);
        if (!fixed.mBakeable)
            return false;

        Baker baker;
        model.accept(baker);
        return true;
    }
}
