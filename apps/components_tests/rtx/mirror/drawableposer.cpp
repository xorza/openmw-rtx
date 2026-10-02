#include <vector>

#include <gtest/gtest.h>

#include <osg/BoundingSphere>
#include <osg/FrameStamp>
#include <osg/Group>
#include <osg/Matrix>
#include <osg/MatrixTransform>
#include <osg/Vec3d>
#include <osg/ref_ptr>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>
#include <osgUtil/UpdateVisitor>

#include <apps/components_tests/rtx/support/graph.hpp>
#include <components/rtx/mirror/drawableposer.hpp>
#include <components/rtx/mirror/mirrorpass.hpp>
#include <components/sceneutil/riggeometry.hpp>
#include <components/sceneutil/skeleton.hpp>

namespace Rtx
{
    namespace
    {
        /// A unit quad bound rigidly to one bone, with a bound sphere around it so that an
        /// intersector reaches the rig wherever the bone takes it.
        struct Rig
        {
            osg::ref_ptr<SceneUtil::Skeleton> mSkeleton = new SceneUtil::Skeleton;
            osg::ref_ptr<osg::MatrixTransform> mBone = new osg::MatrixTransform;
            osg::ref_ptr<SceneUtil::RigGeometry> mRig = new SceneUtil::RigGeometry;

            Rig()
            {
                mBone->setName("bone");
                mSkeleton->addChild(mBone);

                mRig->setBoneInfo({ SceneUtil::RigGeometry::BoneInfo{ .mName = "bone",
                    .mBoundSphere = osg::BoundingSpheref(osg::Vec3f(0.5f, 0.5f, 0.0f), 1.0f),
                    .mInvBindMatrix = osg::Matrixf::identity() } });
                mRig->setInfluences(std::vector<SceneUtil::RigGeometry::BoneWeights>(
                    4, SceneUtil::RigGeometry::BoneWeights{ { 0, 1.0f } }));
                mRig->setSourceGeometry(Testing::makeQuad());

                osg::ref_ptr<osg::Group> holder = new osg::Group;
                holder->addChild(mRig);
                mSkeleton->addChild(holder);
            }

            /// Moves the bone `along` x and runs the update the game runs before a ray is cast.
            void moveTo(float along, unsigned int traversal)
            {
                mBone->setMatrix(osg::Matrix::translate(along, 0.0, 0.0));
                osgUtil::UpdateVisitor visitor;
                visitor.setTraversalNumber(traversal);
                mSkeleton->accept(visitor);
            }

            /// Whether a ray straight down through the middle of where the quad stands at `along`
            /// meets it.
            bool hitAt(float along)
            {
                osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector = new osgUtil::LineSegmentIntersector(
                    osg::Vec3d(double{ along } + 0.5, 0.5, 5.0), osg::Vec3d(double{ along } + 0.5, 0.5, -5.0));
                osgUtil::IntersectionVisitor visitor(intersector);
                mSkeleton->accept(visitor);
                return intersector->containsIntersections();
            }
        };

        /// **A ray meets the body where the picture shows it, and the body is posed once a frame.**
        /// A bone moved ten units along x carries the quad there, and with nothing posed the ray
        /// meets nothing: the rig's own copy holds the bind pose at the origin. Posed, the ray at
        /// ten meets it. The bone moved on to twenty and posed again in the same frame stays at
        /// ten, so the ray at twenty meets nothing; the next frame poses it there.
        TEST(RtxDrawablePoserTest, aRayMeetsTheBodyWhereThePictureShowsItAndABodyIsPosedOnceAFrame)
        {
            Rig rig;
            rig.moveTo(10.0f, 1);
            ASSERT_FALSE(rig.hitAt(10.0f)) << "the bind pose stands at the origin";
            EXPECT_FALSE(rig.hitAt(0.0f)) << "and outside the bound the update moved";

            Traversals traversals;
            DrawablePoser poser(traversals);
            osg::ref_ptr<osg::FrameStamp> frame = new osg::FrameStamp;
            frame->setFrameNumber(1);

            poser.pose(*rig.mRig, *frame);
            EXPECT_TRUE(rig.hitAt(10.0f)) << "posed where the bone took it";

            rig.moveTo(20.0f, 2);
            poser.pose(*rig.mRig, *frame);
            EXPECT_FALSE(rig.hitAt(20.0f)) << "posed twice in one frame";

            frame->setFrameNumber(2);
            poser.pose(*rig.mRig, *frame);
            EXPECT_TRUE(rig.hitAt(20.0f)) << "and posed again on the next";
        }
    }
}
