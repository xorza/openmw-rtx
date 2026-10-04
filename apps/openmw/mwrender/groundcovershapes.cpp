#include "groundcovershapes.hpp"

#include <algorithm>

#include <osg/Array>
#include <osg/BoundingBox>
#include <osg/Geometry>
#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <osg/Transform>
#include <osg/Vec3d>

#include <components/shader/automaps.hpp>

namespace MWRender
{
    namespace
    {
        /// Whether any transform under the model is one a bake cannot stand in for: one with a
        /// callback, which a controller or a billboard writes its matrix through, or one that is no
        /// plain matrix, whose identity nothing here sets. Hidden nodes as well, which a controller
        /// may show.
        class FixedTransforms : public osg::NodeVisitor
        {
        public:
            FixedTransforms()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
                setNodeMaskOverride(~0u);
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
        /// to the identity on the way back up, once every geometry under it has read it. Hidden
        /// geometry too, since its transforms are set to the identity all the same.
        class Baker : public osg::NodeVisitor
        {
        public:
            Baker()
                : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            {
                setNodeMaskOverride(~0u);
            }

            void apply(osg::Geometry& geometry) override
            {
                // By hand and not by `osg::computeLocalToWorld`, whose visitor skips a hidden transform.
                osg::Matrix placed;
                for (osg::Node* const node : getNodePath())
                    if (const osg::Transform* const transform = node->asTransform())
                        transform->computeLocalToWorldMatrix(placed, this);
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

                // A tangent lies in the surface, so it takes the transform itself. `recreateShaders`
                // does not build the chunk's tangents again, so the template's are turned here; a
                // mirror flips the bitangent `cross(normal, tangent)` makes, which the sign puts back.
                if (auto* const tangents
                    = dynamic_cast<osg::Vec4Array*>(geometry.getTexCoordArray(Shader::sTangentUnit)))
                {
                    const osg::Vec3d x(placed(0, 0), placed(0, 1), placed(0, 2));
                    const osg::Vec3d y(placed(1, 0), placed(1, 1), placed(1, 2));
                    const osg::Vec3d z(placed(2, 0), placed(2, 1), placed(2, 2));
                    const float handedness = x * (y ^ z) < 0.0 ? -1.0f : 1.0f;
                    for (osg::Vec4f& tangent : *tangents)
                    {
                        osg::Vec3f along
                            = osg::Matrix::transform3x3(osg::Vec3f(tangent.x(), tangent.y(), tangent.z()), placed);
                        along.normalize();
                        tangent = osg::Vec4f(along, tangent.w() * handedness);
                    }
                    tangents->dirty();
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

    float GroundcoverShapes::reach(const osg::BoundingBox& box)
    {
        float widest = 0.0f;
        for (unsigned int corner = 0; corner < 8; ++corner)
            widest = std::max(widest, box.corner(corner).length());
        return widest;
    }
}
