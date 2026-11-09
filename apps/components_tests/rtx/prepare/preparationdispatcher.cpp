#include <cstddef>
#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include <components/rtx/prepare/preparationdispatcher.hpp>
#include <components/rtx/preprocess/texture/chainscratch.hpp>

namespace Rtx
{
    namespace
    {
        using Kind = Dispatch::Kind;

        /// A chain whose make needs `texels` bytes of texels, holding `image` bytes of images, for `seconds`.
        ChainNeed chainOf(std::size_t texels, std::uint64_t image = 0, double seconds = 1.0)
        {
            ChainNeed need;
            need.mScratch[ChainBuffer::Texels] = texels;
            need.mImageBytes = image;
            need.mSeconds = seconds;
            return need;
        }

        ChainScratch keeping(std::size_t texels)
        {
            ChainScratch kept;
            kept[ChainBuffer::Texels] = texels;
            return kept;
        }

        /// **With no budget, the largest chain first, a walk once no chain is queued, and the end once nothing
        /// runs**: two threads, two walks. The first walk queues chains of 10, 30 and 20; the other thread
        /// takes 30, the first, its walk ended, takes 20, then 10 goes to the thread that ends first, and the
        /// second walk only once the queue is empty.
        TEST(RtxPreparationDispatcherTest, withNoBudgetTheLargestChainGoesFirst)
        {
            PreparationDispatcher dispatcher(2, 2, std::nullopt);
            const Dispatch walk = dispatcher.next(0, 0.0);
            ASSERT_EQ(walk.mKind, Kind::Walk);
            EXPECT_EQ(walk.mJob, 0u);
            const std::size_t small = dispatcher.queue(chainOf(10));
            const std::size_t large = dispatcher.queue(chainOf(30));
            const std::size_t middle = dispatcher.queue(chainOf(20));

            const Dispatch first = dispatcher.next(1, 0.0);
            ASSERT_EQ(first.mKind, Kind::Chain);
            EXPECT_EQ(first.mJob, large);
            dispatcher.ended(0, ChainScratch{}, 0, 0);
            const Dispatch second = dispatcher.next(0, 0.0);
            ASSERT_EQ(second.mKind, Kind::Chain);
            EXPECT_EQ(second.mJob, middle);
            dispatcher.ended(1, keeping(30), 0, 0);
            const Dispatch third = dispatcher.next(1, 0.0);
            ASSERT_EQ(third.mKind, Kind::Chain);
            EXPECT_EQ(third.mJob, small);
            dispatcher.ended(0, keeping(20), 0, 0);
            const Dispatch walkTwo = dispatcher.next(0, 0.0);
            ASSERT_EQ(walkTwo.mKind, Kind::Walk);
            EXPECT_EQ(walkTwo.mJob, 1u);

            dispatcher.ended(1, keeping(30), 0, 0);
            EXPECT_EQ(dispatcher.next(1, 0.0).mKind, Kind::Wait) << "nothing left, and thread 0 still walks";
            EXPECT_FALSE(dispatcher.isFinished());
            dispatcher.ended(0, keeping(20), 0, 0);
            EXPECT_TRUE(dispatcher.isFinished());
            EXPECT_EQ(dispatcher.next(0, 0.0).mKind, Kind::Finished);
        }

        /// **A chain that does not fit waits, and a smaller one backfills only before the head's time or in
        /// its spare room**: a budget of 100, a running chain holding 60 until second 10, a head of 80. Of the
        /// two smaller chains, 30 for 5 s ends by second 10 and starts; 30 for 20 s neither ends by then nor
        /// fits the head's spare of 100 - 80 = 20, and waits.
        TEST(RtxPreparationDispatcherTest, aSmallerChainBackfillsOnlyBeforeTheHeadsTime)
        {
            PreparationDispatcher dispatcher(3, 1, std::uint64_t{ 100 });
            ASSERT_EQ(dispatcher.next(0, 0.0).mKind, Kind::Walk);
            dispatcher.queue(chainOf(60, 0, 10.0));
            dispatcher.ended(0, ChainScratch{}, 0, 0);
            ASSERT_EQ(dispatcher.next(0, 0.0).mKind, Kind::Chain);
            EXPECT_EQ(dispatcher.getHeld(), 60u);

            const std::size_t head = dispatcher.queue(chainOf(80, 0, 1.0));
            const std::size_t longer = dispatcher.queue(chainOf(30, 0, 20.0));
            const std::size_t shorter = dispatcher.queue(chainOf(30, 0, 5.0));
            const Dispatch backfilled = dispatcher.next(1, 0.0);
            ASSERT_EQ(backfilled.mKind, Kind::Chain);
            EXPECT_EQ(backfilled.mJob, shorter);
            EXPECT_EQ(dispatcher.next(2, 0.0).mKind, Kind::Wait) << "neither the head nor the longer one";

            dispatcher.ended(0, keeping(60), 0, 0);
            dispatcher.ended(1, keeping(30), 0, 0);
            const Dispatch headStarts = dispatcher.next(0, 11.0);
            EXPECT_EQ(headStarts.mKind, Kind::Wait) << "60 and 30 kept, idle, leave no room for 80";
            EXPECT_TRUE(headStarts.mWakes) << "the other idle thread asked to let go";
            const Dispatch letGo = dispatcher.next(1, 11.0);
            ASSERT_EQ(letGo.mKind, Kind::LetGo);
            dispatcher.letGo(1);
            const Dispatch headNow = dispatcher.next(0, 11.0);
            ASSERT_EQ(headNow.mKind, Kind::Chain) << "80 grows 20 past the 60 thread 0 keeps";
            EXPECT_EQ(headNow.mJob, head);
            EXPECT_EQ(dispatcher.getHeld(), 80u);
            EXPECT_EQ(dispatcher.next(2, 11.0).mKind, Kind::Wait) << "30 more than the 20 the head leaves";
            dispatcher.ended(0, keeping(80), 0, 0);
            const Dispatch routed = dispatcher.next(2, 12.0);
            EXPECT_EQ(routed.mKind, Kind::Wait) << "30 grows nothing on thread 0, which keeps 80";
            EXPECT_TRUE(routed.mWakes);
            const Dispatch last = dispatcher.next(0, 12.0);
            ASSERT_EQ(last.mKind, Kind::Chain);
            EXPECT_EQ(last.mJob, longer);
            EXPECT_EQ(dispatcher.getHeld(), 80u) << "within what thread 0 kept";
        }
        /// **A chain past the whole budget runs alone, once nothing runs and every other thread let go**:
        /// a budget of 100 and a chain of 150. While a walk runs it waits; the walk's thread, ended keeping
        /// 40, lets go first, then takes it alone, and the smaller chain queued with it waits for its end, and
        /// for the 150 kept past the budget to be let go.
        TEST(RtxPreparationDispatcherTest, aChainPastTheBudgetRunsAlone)
        {
            PreparationDispatcher dispatcher(2, 1, std::uint64_t{ 100 });
            ASSERT_EQ(dispatcher.next(0, 0.0).mKind, Kind::Walk);
            const std::size_t huge = dispatcher.queue(chainOf(150));
            const std::size_t small = dispatcher.queue(chainOf(10));
            EXPECT_EQ(dispatcher.next(1, 0.0).mKind, Kind::Wait) << "a walk runs";
            dispatcher.ended(0, keeping(40), 0, 0);
            EXPECT_EQ(dispatcher.next(0, 0.0).mKind, Kind::LetGo);
            dispatcher.letGo(0);
            const Dispatch alone = dispatcher.next(0, 0.0);
            ASSERT_EQ(alone.mKind, Kind::Chain);
            EXPECT_EQ(alone.mJob, huge);
            EXPECT_TRUE(alone.mAlone);
            EXPECT_EQ(dispatcher.next(1, 0.0).mKind, Kind::Wait) << "nothing beside it";
            dispatcher.ended(0, keeping(150), 0, 0);
            const Dispatch after = dispatcher.next(1, 1.0);
            ASSERT_EQ(after.mKind, Kind::Wait) << "the 150 thread 0 keeps is past the budget";
            EXPECT_TRUE(after.mWakes);
            EXPECT_EQ(dispatcher.next(0, 1.0).mKind, Kind::LetGo);
            dispatcher.letGo(0);
            const Dispatch routed = dispatcher.next(0, 1.0);
            ASSERT_EQ(routed.mKind, Kind::Chain);
            EXPECT_EQ(routed.mJob, small);
            EXPECT_FALSE(routed.mAlone);
        }

        /// **A queued chain's images count from its walk to its end, and a walk counts as the largest walk's
        /// images until it ends**: a walk that met 70 queues a chain of 10 holding 60, which counts 60 queued
        /// and 70 running; a third thread's walk, counted 70, does not fit the 100 beside them while it
        /// runs, and fits once it ended.
        TEST(RtxPreparationDispatcherTest, imagesCountFromTheirWalkToTheirChainsEnd)
        {
            PreparationDispatcher dispatcher(3, 3, std::uint64_t{ 100 });
            ASSERT_EQ(dispatcher.next(0, 0.0).mKind, Kind::Walk);
            ASSERT_EQ(dispatcher.next(1, 0.0).mKind, Kind::Walk) << "counted as no image, the largest met so far";
            const std::size_t chain = dispatcher.queue(chainOf(10, 60));
            EXPECT_EQ(dispatcher.getHeld(), 60u);
            dispatcher.ended(0, ChainScratch{}, 0, 70);
            const Dispatch made = dispatcher.next(0, 0.0);
            ASSERT_EQ(made.mKind, Kind::Chain);
            EXPECT_EQ(made.mJob, chain);
            EXPECT_EQ(dispatcher.getHeld(), 70u) << "its 60 of images and 10 of scratch";
            EXPECT_EQ(dispatcher.next(2, 0.0).mKind, Kind::Wait) << "70 beside 70 is past 100";
            dispatcher.ended(0, keeping(10), 0, 0);
            EXPECT_EQ(dispatcher.getHeld(), 10u) << "the images let go, the scratch kept";
            const Dispatch walk = dispatcher.next(2, 0.0);
            ASSERT_EQ(walk.mKind, Kind::Walk);
            EXPECT_EQ(walk.mJob, 2u);
            EXPECT_EQ(dispatcher.getHeld(), 80u);
            EXPECT_EQ(dispatcher.getHeldMost(), 80u);
        }
        /// **A head that grows less on another idle thread is left to it, and this thread backfills beside
        /// it**: a budget of 100, thread 0 keeping 60, a head of 60 and a chain of 30. Thread 1 asks first: the
        /// head grows nothing on thread 0, so thread 1 takes the 30, which fits beside the head's room, and
        /// thread 0 then takes the head.
        TEST(RtxPreparationDispatcherTest, aHeadIsLeftToTheThreadItGrowsLeast)
        {
            PreparationDispatcher dispatcher(2, 1, std::uint64_t{ 100 });
            ASSERT_EQ(dispatcher.next(0, 0.0).mKind, Kind::Walk);
            const std::size_t head = dispatcher.queue(chainOf(60));
            const std::size_t beside = dispatcher.queue(chainOf(30));
            dispatcher.ended(0, keeping(60), 0, 0);
            const Dispatch backfilled = dispatcher.next(1, 0.0);
            ASSERT_EQ(backfilled.mKind, Kind::Chain);
            EXPECT_EQ(backfilled.mJob, beside);
            const Dispatch routed = dispatcher.next(0, 0.0);
            ASSERT_EQ(routed.mKind, Kind::Chain);
            EXPECT_EQ(routed.mJob, head);
            EXPECT_EQ(dispatcher.getHeld(), 90u) << "60 kept and grown by nothing, and 30";
        }
    }
}
