#include <memory>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Image>
#include <osg/Matrixd>
#include <osg/ref_ptr>

#include <apps/openmw/mwrender/offscreenview.hpp>
#include <apps/openmw/mwrender/rtx/pendingpaints.hpp>
#include <components/sceneutil/imageregion.hpp>

namespace MWRender
{
    namespace
    {
        /// A tile whose picture comes back when the test says.
        class Tile final : public OffscreenView
        {
        public:
            void setView(const osg::Matrixd&) override {}
            void redraw() override {}
            void keepCopy() override { ++mCopiesAsked; }
            const osg::Image* getCopy() override { return mLanded ? mPicture.get() : nullptr; }
            bool isAbandoned() const override { return mAbandoned; }
            MyGUI::ITexture& getTexture() const override { throw std::logic_error("no texture in this test"); }

            osg::ref_ptr<osg::Image> mPicture = new osg::Image;
            bool mLanded = false;
            bool mAbandoned = false;
            int mCopiesAsked = 0;
        };

        struct Painted
        {
            SceneUtil::ImageRegion mWhere;
            const osg::Image* mPicture;
        };

        /// **A paint waits for its picture however soon the local map lets go of the tile**, and
        /// is painted once it lands: a cell crossed quickly after a load stayed black on the world
        /// map for the session when the paint went with the map's hold. The last word for a
        /// rectangle wins, and a reset drops what waits.
        TEST(PendingPaintsTest, aPaintWaitsForItsPictureWhoeverLetsGoOfTheTile)
        {
            PendingPaints pending;
            std::vector<Painted> painted;
            const auto paint = [&](const SceneUtil::ImageRegion& where, const osg::Image& picture) {
                painted.push_back(Painted{ where, &picture });
            };

            const SceneUtil::ImageRegion crossed{ 0, 0, 16, 16 };
            std::shared_ptr<Tile> held = std::make_shared<Tile>();
            Tile* const tile = held.get();
            pending.add(crossed, held);
            EXPECT_EQ(tile->mCopiesAsked, 1) << "the copy is asked for when the paint is";

            // The local map lets go of the tile before its picture came back.
            held.reset();
            pending.finish(paint);
            EXPECT_TRUE(painted.empty());
            ASSERT_EQ(pending.size(), 1u) << "the paint went with the map's hold";

            // The paint holds the last of the tile, which goes once it is painted.
            const osg::ref_ptr<osg::Image> picture = tile->mPicture;
            tile->mLanded = true;
            pending.finish(paint);
            ASSERT_EQ(painted.size(), 1u);
            EXPECT_EQ(painted[0].mWhere, crossed);
            EXPECT_EQ(painted[0].mPicture, picture.get());
            EXPECT_EQ(pending.size(), 0u) << "a paint finished once";

            // Two asks for one rectangle paint the later tile, once; a reset drops what waits.
            const auto first = std::make_shared<Tile>();
            const auto later = std::make_shared<Tile>();
            pending.add(crossed, first);
            pending.add(crossed, later);
            EXPECT_EQ(pending.size(), 1u);
            first->mLanded = true;
            later->mLanded = true;
            painted.clear();
            pending.finish(paint);
            ASSERT_EQ(painted.size(), 1u);
            EXPECT_EQ(painted[0].mPicture, later->mPicture.get());

            pending.add(crossed, std::make_shared<Tile>());
            pending.clear();
            EXPECT_EQ(pending.size(), 0u);

            // **A tile whose scene was left before it was drawn lets go, unpainted**: its picture
            // would be of the world the game went to, and nothing else would ever end its wait.
            const auto left = std::make_shared<Tile>();
            pending.add(crossed, left);
            left->mAbandoned = true;
            painted.clear();
            pending.finish(paint);
            EXPECT_TRUE(painted.empty()) << "a picture of the wrong world was painted";
            EXPECT_EQ(pending.size(), 0u) << "a paint that can never land kept waiting";
        }
    }
}
