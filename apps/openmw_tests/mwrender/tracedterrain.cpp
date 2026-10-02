#include <algorithm>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Group>
#include <osg/Vec2i>
#include <osg/Vec3d>
#include <osg/Vec3f>
#include <osg/Vec4f>
#include <osg/ref_ptr>
#include <osgUtil/IntersectionVisitor>
#include <osgUtil/LineSegmentIntersector>

#include <apps/components_tests/rtx/support/fakeland.hpp>
#include <apps/openmw/mwrender/rtx/debugwalk.hpp>
#include <apps/openmw/mwrender/rtx/tracedterrain.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadland.hpp>
#include <components/resource/bgsmfilemanager.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/resource/niffilemanager.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/rtx/frame/debuglines.hpp>
#include <components/terrain/view.hpp>
#include <components/vfs/manager.hpp>

namespace MWRender
{
    namespace
    {
        constexpr unsigned int sTerrainMask = 1u << 8;

        /// Where a ray straight down over (`x`, `y`) meets what stands under `root` with
        /// `sTerrainMask`, or nothing.
        std::optional<osg::Vec3d> groundUnder(osg::Group& root, const double x, const double y)
        {
            osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector = new osgUtil::LineSegmentIntersector(
                osgUtil::LineSegmentIntersector::MODEL, osg::Vec3d(x, y, 10000.0), osg::Vec3d(x, y, -10000.0));
            osgUtil::IntersectionVisitor visitor(intersector);
            visitor.setTraversalMask(sTerrainMask);
            root.accept(visitor);

            if (!intersector->containsIntersections())
                return std::nullopt;

            return intersector->getFirstIntersection().getWorldIntersectPoint();
        }

        /// The ring's word, as a list of the cells it stands ground at.
        struct Stood final : StandingGround
        {
            std::vector<osg::Vec2i> mCells;

            bool standsGround(const osg::Vec2i& cell) const override
            {
                return std::find(mCells.begin(), mCells.end(), cell) != mCells.end();
            }
        };

        /// Where a segment from `from` to `to` meets what stands under `root` with `sTerrainMask`,
        /// nearest first: every meeting, or the nearest alone.
        std::vector<osg::Vec3d> hitsAlong(osg::Group& root, const osg::Vec3d& from, const osg::Vec3d& to,
            osgUtil::Intersector::IntersectionLimit limit)
        {
            osg::ref_ptr<osgUtil::LineSegmentIntersector> intersector
                = new osgUtil::LineSegmentIntersector(osgUtil::LineSegmentIntersector::MODEL, from, to);
            intersector->setIntersectionLimit(limit);
            osgUtil::IntersectionVisitor visitor(intersector);
            visitor.setTraversalMask(sTerrainMask);
            root.accept(visitor);

            std::vector<osg::Vec3d> hits;
            for (const osgUtil::LineSegmentIntersector::Intersection& hit : intersector->getIntersections())
                hits.push_back(hit.getWorldIntersectPoint());
            return hits;
        }

        /// What a `TracedTerrain` is made with, over an empty archive.
        struct Making
        {
            VFS::Manager mVfs;
            Resource::ImageManager mImages{ &mVfs, 0 };
            Resource::NifFileManager mNifs{ &mVfs, nullptr };
            Resource::BgsmFileManager mMaterials{ &mVfs, 0 };
            Resource::SceneManager mScenes{ &mVfs, &mImages, &mNifs, &mMaterials, 0 };
            Rtx::Testing::FakeLand mLand;
            osg::ref_ptr<osg::Group> mSceneRoot = new osg::Group;
            osg::ref_ptr<osg::Group> mWorldRoot = new osg::Group;
            Stood mStood;

            // The borders' shaders are the rasterizer's, and an empty archive holds none of them.
            Making() { mScenes.setShadersEnabled(false); }

            TracedTerrain make()
            {
                return TracedTerrain(
                    *mSceneRoot, *mWorldRoot, mLand, mScenes, mStood, sTerrainMask, ESM::Cell::sDefaultWorldspaceId);
            }
        };

        /// **The ground answers upstream's callers as a world with no chunks**, and draws
        /// upstream's cell borders. The preloader asks for a view and resets it on a worker thread.
        /// `tb` stands a line strip over each loaded cell's south and east edges, ten units over the
        /// storage's height, straight under the world root and under `Mask_Debug`, where
        /// `DebugWalk` reads it: forty segments a side, so eighty lines of two vertices.
        TEST(RtxTracedTerrainTest, aViewIsHandedOutAndTheBordersStandOverEachLoadedCell)
        {
            Making making;
            making.mLand.mWithData.push_back(osg::Vec2i(0, 0));
            making.mLand.mWithData.push_back(osg::Vec2i(1, 0));
            TracedTerrain ground = making.make();

            EXPECT_EQ(making.mSceneRoot->getNumChildren(), 1u) << "the terrain root the game masks and finds";

            const osg::ref_ptr<Terrain::View> view = ground.createView();
            ASSERT_NE(view, nullptr);
            view->reset();

            ground.loadCell(0, 0);
            EXPECT_EQ(making.mWorldRoot->getNumChildren(), 0u) << "a border before `tb`";
            ground.setBordersVisible(true);
            EXPECT_TRUE(ground.getBordersVisible());
            ASSERT_EQ(making.mWorldRoot->getNumChildren(), 1u);
            EXPECT_EQ(making.mWorldRoot->getChild(0)->getNodeMask(), static_cast<unsigned int>(Mask_Debug));

            DebugWalk walk;
            const Rtx::DebugLines lines = walk.walk(*making.mWorldRoot);
            ASSERT_EQ(lines.mLines.size(), 160u);
            EXPECT_TRUE(lines.mTriangles.empty());

            // The fake land answers every height with the default, -2048, and the border stands ten
            // over it. A side is 8192 in forty steps of 204.8, black and yellow by turns, and the
            // yellow's alpha of nought is not read where nothing blends.
            constexpr float z = static_cast<float>(ESM::Land::DEFAULT_HEIGHT) + 10.0f;
            EXPECT_EQ(lines.mLines[0].mPosition, osg::Vec3f(0.0f, 0.0f, z));
            EXPECT_EQ(lines.mLines[0].mColour, osg::Vec4f(0.0f, 0.0f, 0.0f, 1.0f));
            EXPECT_EQ(lines.mLines[1].mPosition, osg::Vec3f(204.8f, 0.0f, z));
            EXPECT_EQ(lines.mLines[1].mColour, osg::Vec4f(1.0f, 1.0f, 0.0f, 1.0f));
            EXPECT_EQ(lines.mLines[159].mPosition, osg::Vec3f(8192.0f, 8192.0f, z));

            // A cell that arrives under `tb` brings its border, and one that leaves takes its own.
            ground.loadCell(1, 0);
            EXPECT_EQ(making.mWorldRoot->getNumChildren(), 2u);
            ground.unloadCell(0, 0);
            EXPECT_EQ(making.mWorldRoot->getNumChildren(), 1u);

            ground.setBordersVisible(false);
            EXPECT_FALSE(ground.getBordersVisible());
            EXPECT_EQ(making.mWorldRoot->getNumChildren(), 0u);
        }

        /// A loaded cell stands ground for the intersector at the storage's own height, and an
        /// unloaded one stands none.
        ///
        /// **`RenderingManager::castRay` walks the scene graph under `Mask_Terrain`**, and the
        /// ring's ground is on the device where no `osgUtil` visitor reaches. The fake land is a
        /// plane of eight units a column and sixteen a row, so its middle vertex, thirty-two of
        /// each, stands at 768 — and a vertex, so the answer is exact rather than interpolated.
        /// The cell is loaded twice with an unload between, because the second load stands on the
        /// grid the first gave back.
        TEST(RtxTracedTerrainTest, aLoadedCellAnswersADownwardRayAtTheLandsHeight)
        {
            Making making;
            making.mLand.mWithData.push_back(osg::Vec2i(0, 0));
            making.mLand.mWithData.push_back(osg::Vec2i(1, 0));
            TracedTerrain ground = making.make();
            osg::Group* const sceneRoot = making.mSceneRoot.get();

            constexpr double cell = static_cast<double>(Rtx::Testing::FakeLand::sCellSize);
            constexpr double middle = 0.5 * cell;
            EXPECT_FALSE(groundUnder(*sceneRoot, middle, middle).has_value()) << "ground before any cell was loaded";

            for (int again = 0; again < 2; ++again)
            {
                ground.loadCell(0, 0);
                const std::optional<osg::Vec3d> met = groundUnder(*sceneRoot, middle, middle);
                ASSERT_TRUE(met.has_value()) << "the loaded cell stood no ground, on load " << again;
                EXPECT_NEAR(met->z(), static_cast<double>(Rtx::Testing::FakeLand::heightAt(32, 32)), 1e-3);
                EXPECT_NEAR(met->x(), middle, 1e-3);
                EXPECT_NEAR(met->y(), middle, 1e-3);

                // A quarter of the way across, which is not a vertex: column and row sixteen
                // stand at 384, and the diamond's triangles are planes through the vertices, so
                // the plane's own value is the answer wherever a ray lands on it.
                constexpr double quarter = 0.25 * cell;
                const std::optional<osg::Vec3d> slope = groundUnder(*sceneRoot, quarter + 10.0, quarter + 30.0);
                ASSERT_TRUE(slope.has_value());
                EXPECT_NEAR(slope->z(), 8.0 * (16.0 + 10.0 / 128.0) + 16.0 * (16.0 + 30.0 / 128.0), 1e-2);

                ground.unloadCell(0, 0);
                EXPECT_FALSE(groundUnder(*sceneRoot, middle, middle).has_value()) << "ground after the cell left";
            }

            // The other cell of the same worldspace is not the one that was loaded.
            ground.loadCell(0, 0);
            EXPECT_FALSE(groundUnder(*sceneRoot, middle + cell, middle).has_value());

            // Two cells, and the first to arrive leaves first: the one that stays is still found,
            // stood once however often it is loaded, and taken down by its own unload.
            const osg::Group& terrainRoot = *sceneRoot->getChild(0)->asGroup();
            ground.loadCell(1, 0);
            EXPECT_EQ(terrainRoot.getNumChildren(), 3u) << "the two grids and the distance's answer";
            ground.unloadCell(0, 0);
            EXPECT_FALSE(groundUnder(*sceneRoot, middle, middle).has_value());
            ground.loadCell(1, 0);
            EXPECT_EQ(terrainRoot.getNumChildren(), 2u) << "a standing cell was stood twice";
            ground.unloadCell(1, 0);
            EXPECT_EQ(terrainRoot.getNumChildren(), 1u) << "the cell that stayed was lost";
        }

        /// **Past the loaded cells a ray meets the ground the ring stands**, as it meets the
        /// rasterizer's distant chunks, and nothing where the ring stands none.
        ///
        /// The fake land is the plane `8 column + 16 row` in every cell, so a level ray along +x at
        /// 700, ten units off the middle row — row 32 + 10/128, which is 513.25 — first goes under
        /// a cell's ground at column (700 - 513.25) / 8 = 23.34375, 2988 units into it. A cell
        /// starts back at 513.25 under the ray, so each stood cell is met once: in cell 2 at
        /// 2 × 8192 + 2988 = 19372, and in cell 3 at 27564. The nearest alone is cell 2's.
        TEST(RtxTracedTerrainTest, pastTheLoadedCellsARayMeetsTheGroundTheRingStands)
        {
            Making making;
            for (int x = 0; x <= 4; ++x)
                making.mLand.mWithData.push_back(osg::Vec2i(x, 0));
            TracedTerrain ground = making.make();
            ground.loadCell(0, 0);

            // The scene's bound is the one loaded grid's, half a cell's diagonal and the slope
            // over it, and not the land's: the traced map stands its eye at the top of it.
            constexpr double cell = static_cast<double>(Rtx::Testing::FakeLand::sCellSize);
            EXPECT_LT(static_cast<double>(making.mSceneRoot->getBound().radius()), cell);

            const osg::Vec3d from(1.0 * cell, 0.5 * cell + 10.0, 700.0);
            const osg::Vec3d to(4.9 * cell, 0.5 * cell + 10.0, 700.0);

            EXPECT_TRUE(hitsAlong(*making.mSceneRoot, from, to, osgUtil::Intersector::NO_LIMIT).empty())
                << "ground the ring does not stand";

            making.mStood.mCells = { osg::Vec2i(2, 0), osg::Vec2i(3, 0) };
            const std::vector<osg::Vec3d> every
                = hitsAlong(*making.mSceneRoot, from, to, osgUtil::Intersector::NO_LIMIT);
            ASSERT_EQ(every.size(), 2u);
            EXPECT_NEAR(every[0].x(), 19372.0, 1e-2);
            EXPECT_NEAR(every[0].z(), 700.0, 1e-3);
            EXPECT_NEAR(every[1].x(), 27564.0, 1e-2);

            const std::vector<osg::Vec3d> nearest
                = hitsAlong(*making.mSceneRoot, from, to, osgUtil::Intersector::LIMIT_NEAREST);
            ASSERT_EQ(nearest.size(), 1u);
            EXPECT_NEAR(nearest[0].x(), 19372.0, 1e-2);

            // A loaded cell answers with its own grid and not a second time from the distance.
            ground.loadCell(2, 0);
            EXPECT_EQ(hitsAlong(*making.mSceneRoot, from, to, osgUtil::Intersector::NO_LIMIT).size(), 2u);

            // And a segment beside the land the storage has crosses none of the cells it passes,
            // whatever its length: two hundred and forty thousand of them here.
            EXPECT_TRUE(hitsAlong(*making.mSceneRoot, osg::Vec3d(-1.0e9, -1.0e9, 700.0),
                osg::Vec3d(-1.0e9, 1.0e9, 700.0), osgUtil::Intersector::NO_LIMIT)
                            .empty());
        }
    }
}
