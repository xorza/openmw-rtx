#include <gtest/gtest.h>

#include <osg/Group>
#include <osg/Matrix>
#include <osg/Node>
#include <osg/StateSet>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

#include <components/rtx/mirror/frozenroots.hpp>
#include <components/rtx/mirror/released.hpp>

namespace Rtx
{
    namespace
    {
        /// **A held face keeps what it names, so a new object never reads as an old one.** A root frozen with one
        /// child shows the face it was frozen at, and none that moved it, changed its identity, its count of children,
        /// its state set or its first child. Once the game takes the child away the face still holds it, so no new
        /// child can be made at its address and the one put in its place is another face. A face that becomes one
        /// moved lets go of nothing, one with a new child lets go of the old child alone, and a released face hands
        /// what it held to the sink, which then holds it alone.
        TEST(RtxFrozenRootsTest, aHeldFaceKeepsWhatItNamesSoANewObjectNeverReadsAsAnOldOne)
        {
            osg::ref_ptr<osg::Group> root = new osg::Group;
            osg::ref_ptr<osg::Node> child = new osg::Node;
            root->addChild(child);
            const FrozenRoots::Face face = FrozenRoots::Face::of(*root, osg::Matrix::identity(), 7);
            Released released;
            FrozenRoots::HeldFace held;
            held.become(face, released);
            EXPECT_TRUE(held.shows(face));
            EXPECT_TRUE(released.isEmpty()) << "a face held nothing before";

            EXPECT_FALSE(held.shows(FrozenRoots::Face::of(*root, osg::Matrix::translate(1.0, 0.0, 0.0), 7))) << "moved";
            EXPECT_FALSE(held.shows(FrozenRoots::Face::of(*root, osg::Matrix::identity(), 8))) << "another identity";
            root->getOrCreateStateSet();
            EXPECT_FALSE(held.shows(FrozenRoots::Face::of(*root, osg::Matrix::identity(), 7))) << "a state set";
            root->setStateSet(nullptr);
            osg::ref_ptr<osg::Node> second = new osg::Node;
            root->addChild(second);
            EXPECT_FALSE(held.shows(FrozenRoots::Face::of(*root, osg::Matrix::identity(), 7))) << "two children";
            root->removeChild(second.get());
            EXPECT_TRUE(held.shows(FrozenRoots::Face::of(*root, osg::Matrix::identity(), 7))) << "as frozen again";

            const osg::observer_ptr<osg::Node> watched(child.get());
            root->removeChild(child.get());
            child = nullptr;
            ASSERT_TRUE(watched.valid()) << "the game let go of a child the face still names";
            root->addChild(new osg::Node);
            EXPECT_FALSE(held.shows(FrozenRoots::Face::of(*root, osg::Matrix::identity(), 7)))
                << "a new child read as the one it replaced";

            const osg::Matrix moved = osg::Matrix::translate(1.0, 0.0, 0.0);
            root->removeChildren(0, 1);
            root->addChild(watched.get());
            held.become(FrozenRoots::Face::of(*root, moved, 7), released);
            EXPECT_TRUE(held.shows(FrozenRoots::Face::of(*root, moved, 7)));
            EXPECT_TRUE(released.isEmpty()) << "a moved face lets go of an object it still names";

            osg::ref_ptr<osg::Node> replacing = new osg::Node;
            root->removeChildren(0, 1);
            root->addChild(replacing);
            held.become(FrozenRoots::Face::of(*root, moved, 7), released);
            EXPECT_EQ(held.mFirstChild.get(), replacing.get());
            ASSERT_EQ(released.get().size(), 1u) << "the old child, and no state set or controller it never had";
            EXPECT_EQ(released.get().front(), watched.get());
            released.clear();
            EXPECT_FALSE(watched.valid());

            const osg::observer_ptr<osg::Node> replacingWatched(replacing.get());
            root->removeChildren(0, 1);
            replacing = nullptr;
            held.release(released);
            EXPECT_EQ(held.mFirstChild, nullptr);
            ASSERT_EQ(released.get().size(), 1u);
            EXPECT_TRUE(replacingWatched.valid()) << "the sink holds it until the game's own is released";
            released.clear();
            EXPECT_FALSE(replacingWatched.valid());
        }
    }
}
