#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Callback>
#include <osg/Geometry>
#include <osg/Group>
#include <osg/LOD>
#include <osg/Matrix>
#include <osg/MatrixTransform>
#include <osg/Matrixf>
#include <osg/Node>
#include <osg/StateSet>
#include <osg/Switch>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/mirror/extractor/fixture.hpp>
#include <apps/components_tests/rtx/support/graph.hpp>
#include <components/rtx/mirror/cells/cellring.hpp>
#include <components/rtx/mirror/cells/cellworld.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/mirror/sceneextractor.hpp>
#include <components/rtx/scene/placementtable.hpp>

namespace Rtx::Testing
{
    namespace
    {
        /// The world's shape as the game builds it — a root, a cell group, and the references
        /// under it, each a transform over its shapes — walked as the world is: through
        /// `extractWorld`, the one walk that freezes, with the reference roots at the stamp depth
        /// production states, two, and a ring told of no world, so the graph is all it stands.
        class RtxFrozenSubtreeTest : public RtxSceneExtractorTest
        {
        protected:
            RtxFrozenSubtreeTest()
            {
                mExtractor.setStampDepth(2);
                mRoot->addChild(mCell);
            }

            ~RtxFrozenSubtreeTest() override
            {
                mRing.follow(WorldAround{});
                mExtractor.detach(mRing);
            }

            /// A reference at `place`, holding one quad of its own.
            osg::ref_ptr<osg::MatrixTransform> addReference(const osg::Vec3f& place)
            {
                osg::ref_ptr<osg::MatrixTransform> reference = new osg::MatrixTransform(osg::Matrix::translate(place));
                reference->addChild(makeQuad());
                mCell->addChild(reference);
                return reference;
            }

            /// One frame: the walk, and the sweep that follows it.
            ExtractionStats frame()
            {
                mScene.clearPlacement();
                mRing.follow(WorldAround{});
                const ExtractionStats stats
                    = mExtractor.extractWorld(*mRoot, osg::Matrixf::identity(), 0, mFrame++, mRing);
                mExtractor.retire();
                return stats;
            }

            /// Where the one placement standing stands.
            osg::Vec3f standing() const
            {
                osg::Vec3f found(-1.0f, -1.0f, -1.0f);
                mScene.placements().forEachPlaced(
                    [&](Index, const PlacementRow& row) { found = osg::Vec3f() * row.mInstance.mTransform; });
                return found;
            }

            osg::ref_ptr<osg::Group> mRoot = new osg::Group;
            osg::ref_ptr<osg::Group> mCell = new osg::Group;
            std::size_t mFrame = 1;
            CellRing mRing{ mExtractor };
        };

        /// **A reference nothing changes is walked once and passed after**, its rows kept through
        /// every sweep it is not walked in; **moved, it is walked and placed where it went**, walked
        /// again on the frame it first stands still there, and frozen then; **taken off the graph,
        /// it is swept.**
        ///
        /// What a walk resolved is what `mMeshesReused` counts: one for the quad on a frame that
        /// walks the reference, and nought on one that passes it, while `mInstances` counts the
        /// placement either way, as the report reads it, and `mPassedFrozen` the quad a pass stood.
        TEST_F(RtxFrozenSubtreeTest, aStillReferenceIsPassedAndKeptAndAMovedOneIsWalkedAgain)
        {
            const osg::ref_ptr<osg::MatrixTransform> reference = addReference(osg::Vec3f(10.0f, 0.0f, 0.0f));

            const ExtractionStats first = frame();
            EXPECT_EQ(first.mMeshesAdded, 1u);
            EXPECT_EQ(first.mInstances, 1u);

            for (int again = 0; again < 3; ++again)
            {
                const ExtractionStats passed = frame();
                EXPECT_EQ(passed.mMeshesReused, 0u) << "a frozen reference was walked";
                EXPECT_EQ(passed.mInstances, 1u) << "a passed reference was not counted";
                EXPECT_EQ(passed.mPassedFrozen, 1u) << "what a pass stood was not counted";
                EXPECT_EQ(mScene.placements().getCounts().mPlaced, 1u) << "a sweep dropped what a hold keeps";
                EXPECT_EQ(standing(), osg::Vec3f(10.0f, 0.0f, 0.0f));
            }

            reference->setMatrix(osg::Matrix::translate(0.0f, 20.0f, 0.0f));
            const ExtractionStats moved = frame();
            EXPECT_EQ(moved.mMeshesReused, 1u) << "a moved reference was passed";
            EXPECT_EQ(moved.mPassedFrozen, 0u);
            EXPECT_EQ(standing(), osg::Vec3f(0.0f, 20.0f, 0.0f));

            EXPECT_EQ(frame().mMeshesReused, 1u) << "it froze on the frame it moved";
            const ExtractionStats refrozen = frame();
            EXPECT_EQ(refrozen.mMeshesReused, 0u) << "it did not freeze where it went";
            EXPECT_EQ(standing(), osg::Vec3f(0.0f, 20.0f, 0.0f));

            mCell->removeChild(reference);
            frame();
            EXPECT_EQ(mScene.placements().getCounts().mPlaced, 0u) << "a frozen reference outlived its node";
        }

        /// **A reference that changes on its own is walked on every frame**: a controller on its
        /// root, a state set a controller writes, a switch anywhere under it. And **what the game
        /// hangs on a frozen root thaws it**: a state set, which an enchantment's glow is, or a child.
        /// **A level of detail does not change**: the walk takes the nearest level, which the ranges
        /// choose and not the eye, so its reference freezes standing that level alone.
        TEST_F(RtxFrozenSubtreeTest, aReferenceThatChangesIsWalkedEveryFrameAndOneTheGameChangesThaws)
        {
            const std::vector<std::pair<const char*, std::function<void(osg::MatrixTransform&)>>> changing{
                { "a controller", [](osg::MatrixTransform& root) { root.setUpdateCallback(new osg::Callback); } },
                { "an animated state set",
                    [](osg::MatrixTransform& root) {
                        root.getOrCreateStateSet()->setUpdateCallback(new osg::StateSet::Callback);
                    } },
                { "a switch",
                    [](osg::MatrixTransform& root) {
                        osg::ref_ptr<osg::Switch> branches = new osg::Switch;
                        branches->addChild(makeQuad(), true);
                        root.addChild(branches);
                    } },
            };

            for (const auto& [what, make] : changing)
            {
                const osg::ref_ptr<osg::MatrixTransform> reference = addReference(osg::Vec3f());
                make(*reference);

                frame();
                for (int again = 0; again < 2; ++again)
                    EXPECT_GT(frame().mMeshesReused, 0u) << what << " froze";

                mCell->removeChild(reference);
                frame();
            }

            {
                const osg::ref_ptr<osg::MatrixTransform> leveled = addReference(osg::Vec3f());
                osg::ref_ptr<osg::LOD> levels = new osg::LOD;
                levels->addChild(makeQuad(), 100.0f, 1000.0f);
                levels->addChild(makeQuad(), 0.0f, 100.0f);
                leveled->addChild(levels);

                frame();
                const ExtractionStats passed = frame();
                EXPECT_EQ(passed.mMeshesReused, 0u) << "a level of detail kept its reference walked";
                // Its own quad and the nearer level, and not the farther one.
                EXPECT_EQ(passed.mPassedFrozen, 2u);

                mCell->removeChild(leveled);
                frame();
            }

            const osg::ref_ptr<osg::MatrixTransform> reference = addReference(osg::Vec3f());
            frame();
            ASSERT_EQ(frame().mMeshesReused, 0u);

            reference->getOrCreateStateSet();
            EXPECT_EQ(frame().mMeshesReused, 1u) << "a state set hung on the root left it frozen";
            EXPECT_EQ(frame().mMeshesReused, 1u) << "it froze on the frame the state set was hung";
            EXPECT_EQ(frame().mMeshesReused, 0u);

            reference->addChild(makeQuad());
            const ExtractionStats grown = frame();
            EXPECT_EQ(grown.mMeshesReused, 1u) << "a child given to the root left it frozen";
            EXPECT_EQ(grown.mMeshesAdded, 1u) << "the child it was given";
            EXPECT_EQ(mScene.placements().getCounts().mPlaced, 2u);
        }

        /// **A reference the game moves on every frame — a door turning — is walked on every frame
        /// and freezes on none**, placed where each frame put it; the first frame it stands still is
        /// walked once more, and the one after passes it. Freezing it on each frame it moved held
        /// and gave back every entry it stands on, and allocated and freed its run, on every frame
        /// of the motion.
        TEST_F(RtxFrozenSubtreeTest, aReferenceMovedOnEveryFrameIsWalkedAndFreezesOnceItStandsStill)
        {
            const osg::ref_ptr<osg::MatrixTransform> reference = addReference(osg::Vec3f());
            frame();
            ASSERT_EQ(frame().mMeshesReused, 0u) << "a still reference froze";

            for (int step = 1; step <= 4; ++step)
            {
                reference->setMatrix(osg::Matrix::translate(static_cast<float>(step), 0.0f, 0.0f));
                EXPECT_EQ(frame().mMeshesReused, 1u) << "step " << step << " passed a moving reference";
                EXPECT_EQ(standing(), osg::Vec3f(static_cast<float>(step), 0.0f, 0.0f)) << "step " << step;
                EXPECT_EQ(mScene.placements().getCounts().mPlaced, 1u) << "step " << step;
            }

            EXPECT_EQ(frame().mMeshesReused, 1u) << "it froze on a frame it moved";
            EXPECT_EQ(frame().mMeshesReused, 0u) << "standing still, it did not freeze";
            EXPECT_EQ(standing(), osg::Vec3f(4.0f, 0.0f, 0.0f));
        }

        /// A node a walk cannot read, which throws where the walk reaches it.
        struct Throwing : osg::Node
        {
            void accept(osg::NodeVisitor&) override { throw std::runtime_error("a node the walk cannot read"); }
        };

        /// **A walk that threw in the middle of a reference's record leaves no record open**: the
        /// walk after it records its own references as any walk does, where a record left open
        /// was a reference root recorded inside another.
        TEST_F(RtxFrozenSubtreeTest, aWalkThatThrowsInsideAReferenceLeavesNoRecordOpen)
        {
            const osg::ref_ptr<osg::MatrixTransform> broken = addReference(osg::Vec3f());
            broken->addChild(new Throwing);
            EXPECT_THROW(frame(), std::runtime_error);

            mCell->removeChild(broken);
            addReference(osg::Vec3f(10.0f, 0.0f, 0.0f));
            EXPECT_EQ(frame().mInstances, 1u);
            EXPECT_EQ(frame().mPassedFrozen, 1u) << "the walk after the throw recorded nothing";
        }

        /// **A view that sees another part of the world walks every reference again**: a mask
        /// that changes thaws what froze under the old one.
        TEST_F(RtxFrozenSubtreeTest, aMaskThatChangesThawsEveryReference)
        {
            addReference(osg::Vec3f());
            addReference(osg::Vec3f(5.0f, 0.0f, 0.0f));
            frame();
            ASSERT_EQ(frame().mMeshesReused, 0u);

            mExtractor.setTraversalMask(~1u);
            EXPECT_EQ(frame().mMeshesReused, 2u);
            EXPECT_EQ(frame().mMeshesReused, 0u) << "they did not freeze under the new mask";
            EXPECT_EQ(mScene.placements().getCounts().mPlaced, 2u);
        }
    }
}
