#include <cmath>

#include <gtest/gtest.h>

#include <osg/Array>
#include <osg/Callback>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/Math>
#include <osg/MatrixTransform>
#include <osg/PositionAttitudeTransform>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <apps/openmw/mwrender/groundcovershapes.hpp>

namespace MWRender
{
    namespace
    {
        /// A model of one shape under two transforms: a scale of two along x, and inside it a turn of
        /// a quarter about z and then a step of ten along x. One vertex at (1, 0, 0) with a normal of
        /// (1, 1, 0) over its root of two.
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

                mRoot->addChild(mOuter);
                mOuter->addChild(mInner);
                mInner->addChild(mShape);
            }

            osg::Vec3f vertex() const { return static_cast<const osg::Vec3Array&>(*mShape->getVertexArray())[0]; }
            osg::Vec3f normal() const { return static_cast<const osg::Vec3Array&>(*mShape->getNormalArray())[0]; }
        };

        /// **The shape's transforms go into its vertex, and the plant's placement then comes after
        /// them**, as it does for every other reference. By hand, in OSG's order, the vertex turned
        /// first: (1, 0, 0) turned a quarter about z is (0, 1, 0), stepped ten is (10, 1, 0), and
        /// scaled two along x is (20, 1, 0). The normal (1, 1, 0) turned is (-1, 1, 0), and under
        /// the scale it takes the inverse transpose, a half along x: (-0.5, 1, 0), whose length is
        /// 1.11803, so (-0.44721, 0.89443, 0). Both transforms are then the identity.
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

            EXPECT_TRUE(model.mOuter->getMatrix().isIdentity());
            EXPECT_TRUE(model.mInner->getMatrix().isIdentity());

            const osg::Vec3f placed = model.vertex() * 2.0f + osg::Vec3f(100.0f, 200.0f, 0.0f);
            EXPECT_NEAR(placed.x(), 140.0f, 1e-4f);
            EXPECT_NEAR(placed.y(), 202.0f, 1e-4f);
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
