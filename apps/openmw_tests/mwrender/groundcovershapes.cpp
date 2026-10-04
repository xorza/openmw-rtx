#include <cmath>

#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/BoundingBox>
#include <osg/Callback>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Math>
#include <osg/MatrixTransform>
#include <osg/PositionAttitudeTransform>
#include <osg/PrimitiveSet>
#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/ref_ptr>

#include <apps/openmw/mwrender/groundcovershapes.hpp>
#include <components/shader/automaps.hpp>

namespace MWRender
{
    namespace
    {
        /// A model of one shape under two transforms: a scale of two along x, and inside it a turn of
        /// a quarter about z and then a step of ten along x. One vertex at (1, 0, 0) with a normal of
        /// (1, 1, 0) and a tangent of (1, -1, 0), each over its root of two, the tangent right-handed.
        struct Model
        {
            osg::ref_ptr<osg::Group> mRoot = new osg::Group;
            osg::ref_ptr<osg::MatrixTransform> mOuter = new osg::MatrixTransform(osg::Matrix::scale(2.0, 1.0, 1.0));
            osg::ref_ptr<osg::MatrixTransform> mInner = new osg::MatrixTransform(
                osg::Matrix::rotate(osg::PI_2, osg::Vec3d(0.0, 0.0, 1.0)) * osg::Matrix::translate(10.0, 0.0, 0.0));
            osg::ref_ptr<osg::Geometry> mShape = new osg::Geometry;

            Model()
            {
                osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
                vertices->push_back(osg::Vec3f(1.0f, 0.0f, 0.0f));
                osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array(osg::Array::BIND_PER_VERTEX);
                normals->push_back(osg::Vec3f(1.0f, 1.0f, 0.0f) / std::sqrt(2.0f));
                mShape->setVertexArray(vertices);
                mShape->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
                osg::ref_ptr<osg::Vec4Array> tangents = new osg::Vec4Array(osg::Array::BIND_PER_VERTEX);
                tangents->push_back(osg::Vec4f(osg::Vec3f(1.0f, -1.0f, 0.0f) / std::sqrt(2.0f), 1.0f));
                mShape->setTexCoordArray(Shader::sTangentUnit, tangents, osg::Array::BIND_PER_VERTEX);
                mShape->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::POINTS, 0, 1));

                mRoot->addChild(mOuter);
                mOuter->addChild(mInner);
                mInner->addChild(mShape);
            }

            osg::Vec3f vertex() const { return static_cast<const osg::Vec3Array&>(*mShape->getVertexArray())[0]; }
            osg::Vec3f normal() const { return static_cast<const osg::Vec3Array&>(*mShape->getNormalArray())[0]; }
            osg::Vec4f tangent() const
            {
                return static_cast<const osg::Vec4Array&>(*mShape->getTexCoordArray(Shader::sTangentUnit))[0];
            }
        };

        /// **The shape's transforms go into its vertex, and the plant's placement then comes after
        /// them**, as it does for every other reference. By hand, in OSG's order, the vertex turned
        /// first: (1, 0, 0) turned a quarter about z is (0, 1, 0), stepped ten is (10, 1, 0), and
        /// scaled two along x is (20, 1, 0). The normal (1, 1, 0) turned is (-1, 1, 0), and under
        /// the scale it takes the inverse transpose, a half along x: (-0.5, 1, 0), whose length is
        /// 1.11803, so (-0.44721, 0.89443, 0). The tangent lies in the surface and takes the transform
        /// itself: (1, -1, 0) turned is (1, 1, 0), scaled is (2, 1, 0), and over its length of 2.23607
        /// (0.89443, 0.44721, 0). Its bitangent, `cross(normal, tangent)` times the sign, was (0, 0, -1)
        /// and is (0, 0, -0.2 - 0.8) = (0, 0, -1) still, so the sign stays one. Both transforms are
        /// then the identity, and the shape reaches the root of 401, 20.02498, from the plant's origin,
        /// where its box's own radius is nought.
        ///
        /// **A mirror flips the sign**, and a hidden transform is baked as a shown one, since a controller
        /// may show it. With the scale at minus two, the vertex is (-20, 1, 0), the
        /// normal (-1, 1, 0) halved along x and turned over, (0.5, 1, 0), so (0.44721, 0.89443, 0), and
        /// the tangent (-2, 1, 0), so (-0.89443, 0.44721, 0). `cross(normal, tangent)` is then
        /// (0, 0, 0.2 + 0.8), where the transformed bitangent is still (0, 0, -1): the sign is minus one.
        ///
        /// A plant at (100, 200, 0) at a scale of two, unturned, which is what `groundcover.vert`
        /// does with the baked vertex: (20, 1, 0) times two, plus the plant, is (140, 202, 0), the
        /// model's own vertex under its transforms and then the plant's. Upstream's order put the
        /// plant first, (102, 200, 0), and turned, stepped and scaled that: (-380, 102, 0).
        TEST(GroundcoverShapesTest, aShapesTransformsGoInsideThePlantsPlacement)
        {
            Model model;
            ASSERT_TRUE(GroundcoverShapes::bakeTransforms(*model.mRoot));

            EXPECT_NEAR(model.vertex().x(), 20.0f, 1e-5f);
            EXPECT_NEAR(model.vertex().y(), 1.0f, 1e-5f);
            EXPECT_NEAR(model.vertex().z(), 0.0f, 1e-5f);

            EXPECT_NEAR(model.normal().x(), -0.44721f, 1e-5f);
            EXPECT_NEAR(model.normal().y(), 0.89443f, 1e-5f);
            EXPECT_NEAR(model.normal().z(), 0.0f, 1e-5f);

            EXPECT_NEAR(model.tangent().x(), 0.89443f, 1e-5f);
            EXPECT_NEAR(model.tangent().y(), 0.44721f, 1e-5f);
            EXPECT_NEAR(model.tangent().z(), 0.0f, 1e-5f);
            EXPECT_EQ(model.tangent().w(), 1.0f);

            EXPECT_TRUE(model.mOuter->getMatrix().isIdentity());
            EXPECT_TRUE(model.mInner->getMatrix().isIdentity());

            const osg::BoundingBox& box = model.mShape->getBoundingBox();
            EXPECT_EQ(box.radius(), 0.0f);
            EXPECT_NEAR(GroundcoverShapes::reach(box), 20.02498f, 1e-4f);

            const osg::Vec3f placed = model.vertex() * 2.0f + osg::Vec3f(100.0f, 200.0f, 0.0f);
            EXPECT_NEAR(placed.x(), 140.0f, 1e-4f);
            EXPECT_NEAR(placed.y(), 202.0f, 1e-4f);

            Model mirrored;
            mirrored.mOuter->setMatrix(osg::Matrix::scale(-2.0, 1.0, 1.0));
            mirrored.mInner->setNodeMask(0u);
            ASSERT_TRUE(GroundcoverShapes::bakeTransforms(*mirrored.mRoot));
            EXPECT_NEAR(mirrored.vertex().x(), -20.0f, 1e-5f);
            EXPECT_NEAR(mirrored.vertex().y(), 1.0f, 1e-5f);
            EXPECT_NEAR(mirrored.normal().x(), 0.44721f, 1e-5f);
            EXPECT_NEAR(mirrored.normal().y(), 0.89443f, 1e-5f);
            EXPECT_NEAR(mirrored.tangent().x(), -0.89443f, 1e-5f);
            EXPECT_NEAR(mirrored.tangent().y(), 0.44721f, 1e-5f);
            EXPECT_EQ(mirrored.tangent().w(), -1.0f) << "a mirror kept the bitangent's side";
            EXPECT_TRUE(mirrored.mInner->getMatrix().isIdentity());
        }

        /// **A model with a transform a bake cannot stand in for is left as it was**: one that a
        /// controller writes every frame, and one that is no plain matrix. Its vertex stays (1, 0, 0)
        /// and its transforms stay what they were.
        TEST(GroundcoverShapesTest, aTransformABakeCannotStandInForLeavesTheModelAlone)
        {
            Model controlled;
            controlled.mInner->setUpdateCallback(new osg::Callback);
            EXPECT_FALSE(GroundcoverShapes::bakeTransforms(*controlled.mRoot));
            EXPECT_EQ(controlled.vertex(), osg::Vec3f(1.0f, 0.0f, 0.0f));
            EXPECT_FALSE(controlled.mOuter->getMatrix().isIdentity());

            Model placed;
            osg::ref_ptr<osg::PositionAttitudeTransform> step = new osg::PositionAttitudeTransform;
            step->setPosition(osg::Vec3d(5.0, 0.0, 0.0));
            placed.mRoot->addChild(step);
            EXPECT_FALSE(GroundcoverShapes::bakeTransforms(*placed.mRoot));
            EXPECT_EQ(placed.vertex(), osg::Vec3f(1.0f, 0.0f, 0.0f));
        }
    }
}
