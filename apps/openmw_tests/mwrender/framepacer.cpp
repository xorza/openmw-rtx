#include <chrono>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/framepacer.hpp>
#include <apps/openmw/mwrender/framesaside.hpp>
#include <components/misc/frameclock.hpp>

namespace MWRender
{
    namespace
    {
        using namespace std::chrono_literals;

        /// **A loop frame steps a wall clock by what stood, at most 200 ms, and a stated clock by its step**: 10 ms
        /// stood is a step of 0.01 s, 300 ms one of 0.2, and a clock stated at a sixtieth steps a sixtieth whatever
        /// stood.
        TEST(MWRenderFramePacerTest, aLoopFrameStepsAWallClockByWhatStoodAndAStatedOneByItsStep)
        {
            const FramePacer::TimePoint now{};
            Misc::LoopClock wall;
            FramePacer paced(wall, 0.0f, now);
            EXPECT_EQ(paced.openLoop(10ms, now), 0.01);
            EXPECT_EQ(paced.openLoop(300ms, now), 0.2) << "a stall steps the world by 200 ms and no more";

            Misc::LoopClock stated(1.0f / 60.0f);
            FramePacer stepped(stated, 0.0f, now);
            EXPECT_EQ(stepped.openLoop(300ms, now), static_cast<double>(1.0f / 60.0f));
            EXPECT_EQ(stated.getNow(), static_cast<double>(1.0f / 60.0f));
        }

        /// **A nested frame steps a wall clock as the loop's does, and holds a stated one**: under a stated step it
        /// stands for nothing and the time stays where the loop put it, since how many a run draws is the wall's
        /// answer.
        TEST(MWRenderFramePacerTest, aNestedFrameStepsAWallClockAndHoldsAStatedOne)
        {
            const FramePacer::TimePoint now{};
            Misc::LoopClock wall;
            FramePacer paced(wall, 0.0f, now);
            EXPECT_EQ(paced.openNested(20ms, now), 0.02f);
            EXPECT_EQ(wall.getStep(), 0.02);

            Misc::LoopClock stated(1.0f / 60.0f);
            FramePacer stepped(stated, 0.0f, now);
            static_cast<void>(stepped.openLoop(0ms, now));
            EXPECT_EQ(stepped.openNested(20ms, now), 0.0f);
            EXPECT_EQ(stated.getStep(), 0.0) << "whatever reads the step inside the nested frame reads nought";
            EXPECT_EQ(stated.getNow(), static_cast<double>(1.0f / 60.0f)) << "and the time stays";
        }

        /// **What stood aside reaches the world's frame**: a loop frame opened at 0, an interface frame from 2 to 7
        /// ms, and the world's frame after it reads 5 ms of the interface and nothing hidden, once.
        TEST(MWRenderFramePacerTest, theInterfacesFramesBetweenTwoWorldFramesReachTheSecond)
        {
            const FramePacer::TimePoint start{};
            Misc::LoopClock wall;
            FramePacer pacer(wall, 0.0f, start);
            static_cast<void>(pacer.openLoop(0ms, start));
            static_cast<void>(pacer.openNested(0ms, start + 2ms));
            pacer.endNested(start + 7ms, FramesAside::NestedEnd::Drawn);

            const AsideSpan aside = pacer.takeAtWorldFrame();
            EXPECT_EQ(aside.mInterface, std::chrono::steady_clock::duration(5ms));
            EXPECT_EQ(aside.mHidden, std::chrono::steady_clock::duration::zero());
            EXPECT_EQ(pacer.takeAtWorldFrame().mInterface, std::chrono::steady_clock::duration::zero())
                << "the count starts again";
        }

        /// **A hold waits to a deadline a period after the last, and a late frame starts the schedule again.** At
        /// 200 frames a second, 5 ms: set at 0, a hold at 1 ms waits 4 to the deadline at 5 and stood 5; a hold at 12
        /// is 7 past it, waits nothing and stood 7, the deadline now 12; a hold at 13 waits 4 to 17. A wake late from
        /// a wait moves no deadline: that wait woke at 18 for 17, the frame took 1 ms, and the hold at 19 waits 3 to
        /// 22, where a schedule from the wake would wait 4. With no limit nothing waits and what stood is what passed.
        TEST(MWRenderFramePacerTest, aHoldWaitsToADeadlineAPeriodAfterTheLastAndALateFrameStartsAgain)
        {
            const FramePacer::TimePoint start{};
            Misc::LoopClock wall;
            FramePacer pacer(wall, 200.0f, start);
            EXPECT_EQ(pacer.getFrameRateLimit(), 200.0f);
            const auto period = std::chrono::duration_cast<FramePacer::Duration>(std::chrono::duration<float>(0.005f));

            const auto expect = [&](const FramePacer::Duration at, const FramePacer::Duration wait,
                                    const FramePacer::Duration stood, const char* what) {
                const FramePacer::HoldPlan plan = pacer.planHold(start + at);
                EXPECT_EQ(plan.mWait, wait) << what;
                EXPECT_EQ(plan.mStood, stood) << what;
            };
            expect(1ms, period - 1ms, period, "early: waits to the deadline");
            expect(12ms, 0ms, 12ms - period, "late: waits nothing, stood what passed");
            expect(13ms, period - 1ms, period, "the schedule again from the late frame");
            expect(19ms, period - 2ms, period, "a late wake moved no deadline");

            FramePacer unlimited(wall, 0.0f, start);
            for (const auto& [at, stood, what] : { std::tuple{ 3ms, 3ms, "no limit: nothing waits" },
                     std::tuple{ 10ms, 7ms, "and what stood is what passed since the last" } })
            {
                const FramePacer::HoldPlan plan = unlimited.planHold(start + at);
                EXPECT_EQ(plan.mWait, FramePacer::Duration::zero()) << what;
                EXPECT_EQ(plan.mStood, FramePacer::Duration(stood)) << what;
            }
        }
    }
}
