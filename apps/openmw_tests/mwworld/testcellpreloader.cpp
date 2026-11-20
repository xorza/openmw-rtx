#include <limits>
#include <memory>
#include <span>
#include <thread>

#include <gtest/gtest.h>

#include <osg/Group>
#include <osg/Vec3f>
#include <osg/Vec4i>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/bound.hpp>
#include <apps/components_tests/rtx/support/fakeland.hpp>
#include <apps/openmw/mwworld/cellpreloader.hpp>
#include <apps/openmw/mwworld/positioncellgrid.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/misc/constants.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/workqueue.hpp>
#include <components/terrain/view.hpp>
#include <components/terrain/world.hpp>
#include <components/vfs/manager.hpp>

namespace MWWorld
{
    namespace
    {
        /// A terrain that preloads nothing, and hands out the view the preloader resets on its worker.
        class EmptyTerrain final : public Terrain::World
        {
        public:
            explicit EmptyTerrain(osg::Group* parent)
                : World(parent, new Rtx::Testing::FakeLand, 0, ESM::Cell::sDefaultWorldspaceId)
            {
            }

            std::unique_ptr<Terrain::View> createView() override { return std::make_unique<View>(); }

        private:
            struct View final : Terrain::View
            {
                void reset() override {}
            };
        };

        /// **A terrain preloaded stays loaded for the caches' expiry delay, and no longer**: preloaded at ten seconds
        /// under a delay of five, it is loaded up to fifteen and not at fifteen, within a cell of where it was asked
        /// and not two cells off. The preloader read the delay off the scene manager, whose cache a budget keeps
        /// and which holds none, so a played session found no terrain loaded at any crossing.
        TEST(MWWorldCellPreloaderTest, aTerrainPreloadedStaysLoadedForTheExpiryDelay)
        {
            const VFS::Manager vfs;
            Resource::ResourceSystem resources(&vfs, 5.0, nullptr);
            const osg::ref_ptr<osg::Group> root = new osg::Group;
            EmptyTerrain terrain(root);
            CellPreloader preloader(&resources, nullptr, &terrain, nullptr);
            preloader.setWorkQueue(std::make_shared<SceneUtil::WorkQueue>(1));

            const PositionCellGrid asked{ osg::Vec3f(100.0f, 200.0f, 300.0f), osg::Vec4i(-1, -1, 2, 2) };
            EXPECT_FALSE(preloader.isTerrainLoaded(asked, 10.0)) << "before anything was preloaded";
            preloader.setTerrainPreloadPositions(std::span(&asked, 1));

            // The worker's item, noticed done by the cache's update, which stamps what loaded.
            const Rtx::Testing::WaitBound bound(Rtx::Testing::sHandOverPatience);
            while (!preloader.isTerrainLoaded(asked, 10.0))
            {
                ASSERT_FALSE(bound.passed()) << "the terrain's preload never read as loaded";
                std::this_thread::yield();
                preloader.updateCache(10.0);
            }

            EXPECT_TRUE(preloader.isTerrainLoaded(asked, 14.9));
            EXPECT_FALSE(preloader.isTerrainLoaded(asked, 15.0)) << "past the delay";

            const auto cell = static_cast<float>(Constants::CellSizeInUnits);
            PositionCellGrid near = asked;
            near.mPosition.x() += cell - 1.0f;
            EXPECT_TRUE(preloader.isTerrainLoaded(near, 10.0));
            PositionCellGrid far = asked;
            far.mPosition.x() += 2.0f * cell;
            EXPECT_FALSE(preloader.isTerrainLoaded(far, 10.0));

            // **The delay read is the one last set**, as a run that holds every cache for good sets it. On a system
            // no preloader works over: the preloader's worker updates the caches of the one above while this runs.
            Resource::ResourceSystem held(&vfs, 5.0, nullptr);
            EXPECT_EQ(held.getExpiryDelay(), 5.0);
            held.setExpiryDelay(std::numeric_limits<double>::infinity());
            EXPECT_EQ(held.getExpiryDelay(), std::numeric_limits<double>::infinity());
        }
    }
}
