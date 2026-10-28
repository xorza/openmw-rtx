#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/rtx/viewschedule.hpp>

namespace MWRender
{
    namespace
    {
        /// What a schedule's view is asked, written down: whether it is of the world and waits for
        /// its ground, and what became of it. One may forget another from inside its draw, as a
        /// view the GUI lets go of while its picture is drawn does.
        struct FakeView
        {
            bool mWorld = false;
            bool mWaits = false;
            int mDrawn = 0;
            bool mAbandoned = false;
            ViewSchedule<FakeView>* mSchedule = nullptr;
            FakeView* mForgets = nullptr;

            bool isOfWorld() const { return mWorld; }
            bool waitsForGround(const int& /*ground*/) const { return mWaits; }
            void abandon() { mAbandoned = true; }

            void draw(std::vector<const FakeView*>& order)
            {
                ++mDrawn;
                order.push_back(this);
                if (mForgets != nullptr)
                    mSchedule->forget(*mForgets);
            }
        };

        constexpr int sGround = 0;

        /// **Every subject and up to the budget of the world, in the order asked**: three tiles and
        /// a doll under a budget of two draw the first two tiles and the doll, and the third tile
        /// the flush after. A view asked twice is drawn once.
        TEST(ViewScheduleTest, aFlushDrawsEverySubjectAndTheWorldUpToItsBudget)
        {
            ViewSchedule<FakeView> schedule;
            FakeView first{ .mWorld = true };
            FakeView second{ .mWorld = true };
            FakeView third{ .mWorld = true };
            FakeView doll;
            for (FakeView* view : { &first, &second, &third, &doll, &first })
                schedule.redraw(*view);

            std::vector<const FakeView*> order;
            schedule.draw(2, sGround, order);
            EXPECT_EQ(order, (std::vector<const FakeView*>{ &first, &second, &doll }));
            EXPECT_TRUE(schedule.hasDeferred());
            EXPECT_FALSE(schedule.isDrawing());

            order.clear();
            schedule.draw(2, sGround, order);
            EXPECT_EQ(order, (std::vector<const FakeView*>{ &third }));
            EXPECT_FALSE(schedule.hasDeferred());
            EXPECT_EQ(first.mDrawn, 1) << "a view asked twice was drawn twice";
        }

        /// **A tile whose ground has not stood waits and counts nothing**: under a budget of one,
        /// the tile behind it is drawn in its place, and it is drawn once its ground stands.
        TEST(ViewScheduleTest, aPictureOfTheWorldWaitsForItsGroundOutsideTheBudget)
        {
            ViewSchedule<FakeView> schedule;
            FakeView waiting{ .mWorld = true, .mWaits = true };
            FakeView ready{ .mWorld = true };
            schedule.redraw(waiting);
            schedule.redraw(ready);

            std::vector<const FakeView*> order;
            schedule.draw(1, sGround, order);
            EXPECT_EQ(order, (std::vector<const FakeView*>{ &ready }));

            waiting.mWaits = false;
            order.clear();
            schedule.draw(1, sGround, order);
            EXPECT_EQ(order, (std::vector<const FakeView*>{ &waiting }));
        }

        /// **A view forgotten inside a flush is not drawn**, by the view drawn before it, and the
        /// walk goes on to the one after.
        TEST(ViewScheduleTest, aViewForgottenInsideAFlushIsNotDrawn)
        {
            ViewSchedule<FakeView> schedule;
            FakeView gone;
            FakeView after;
            FakeView forgetting{ .mSchedule = &schedule, .mForgets = &gone };
            for (FakeView* view : { &forgetting, &gone, &after })
                schedule.redraw(*view);

            std::vector<const FakeView*> order;
            schedule.draw(4, sGround, order);
            EXPECT_EQ(order, (std::vector<const FakeView*>{ &forgetting, &after }));
            EXPECT_EQ(gone.mDrawn, 0);
        }

        /// **Leaving the scene gives up the world's pictures and keeps the subjects'.**
        TEST(ViewScheduleTest, leavingTheSceneGivesUpTheWorldsPicturesAlone)
        {
            ViewSchedule<FakeView> schedule;
            FakeView tile{ .mWorld = true };
            FakeView doll;
            schedule.redraw(tile);
            schedule.redraw(doll);

            schedule.leaveScene();
            EXPECT_TRUE(tile.mAbandoned);
            EXPECT_FALSE(doll.mAbandoned);

            std::vector<const FakeView*> order;
            schedule.draw(4, sGround, order);
            EXPECT_EQ(order, (std::vector<const FakeView*>{ &doll }));
        }
    }
}
