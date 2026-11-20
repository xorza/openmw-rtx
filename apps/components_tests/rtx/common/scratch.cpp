#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtx/common/letgo.hpp>
#include <components/rtx/common/scratch.hpp>

namespace Rtx
{
    namespace
    {
        struct Reading : Lent
        {
            std::int32_t mValue = 0;

            void reuse() { reuseKeeping(*this); }
        };

        /// Fills a reading with `value`, for a take.
        auto valued(const std::int32_t value)
        {
            return [value](Reading& reading) { reading.mValue = value; };
        }

        /// **A buffer keeps its room within twice what it holds, and shrinks to what it holds past that, keeping
        /// it**: eight slots kept under four held, and three left under three, which is the boundary `trimSlack`
        /// draws; an empty buffer gives its room back, and one with no room is left as it is.
        TEST(RtxScratchTest, aBufferKeepsItsRoomUpToTwiceWhatItHoldsAndShrinksToItPastThat)
        {
            const auto trimmed = [](const std::size_t held) {
                std::vector<std::int32_t> buffer;
                buffer.reserve(8);
                EXPECT_EQ(buffer.capacity(), 8u);
                for (std::size_t at = 0; at < held; ++at)
                    buffer.push_back(static_cast<std::int32_t>(10 * at));
                trimSlack(buffer);
                return buffer;
            };

            EXPECT_EQ(trimmed(4).capacity(), 8u) << "eight is twice four";
            EXPECT_EQ(trimmed(5).capacity(), 8u);
            EXPECT_EQ(trimmed(3).capacity(), 3u) << "eight is more than twice three";
            EXPECT_EQ(trimmed(3), (std::vector<std::int32_t>{ 0, 10, 20 })) << "what it held, kept";
            EXPECT_EQ(trimmed(0).capacity(), 0u);

            std::vector<std::int32_t> none;
            trimSlack(none);
            EXPECT_EQ(none.capacity(), 0u);
        }

        /// **A spare is held by whoever `lend`s it, and given back once the last of them releases
        /// it.** The pool counts, so every lent type has one count and one assert: a cell's, a
        /// model's and a texture's holds are the same bookkeeping.
        /// **A buffer let go of holds no room**, where one assigned an empty brace list keeps all 100 of its
        /// elements' room: the assignment of an empty initializer list.
        TEST(RtxScratchTest, aBufferLetGoOfHoldsNoRoom)
        {
            std::vector<std::int32_t> braced(100);
            braced = {};
            EXPECT_EQ(braced.capacity(), 100u) << "a brace list gave its room back, and this proves nothing";

            std::vector<std::int32_t> held(100);
            letGoOf(held);
            EXPECT_TRUE(held.empty());
            EXPECT_EQ(held.capacity(), 0u);
        }

        TEST(RtxSparesTest, anObjectIsSpareAgainWhenItsLastHolderReleasesIt)
        {
            Spares<Reading> spares;

            Reading& first = spares.take(valued(1));
            EXPECT_EQ(first.mValue, 1) << "handed out filled";
            EXPECT_EQ(first.mLent, 0u) << "taken is not yet held";
            EXPECT_EQ(spares.size(), 1u);

            spares.lend(first);
            spares.lend(first);
            EXPECT_EQ(first.mLent, 2u);

            EXPECT_FALSE(spares.release(first)) << "one holder is left";
            EXPECT_TRUE(spares.release(first)) << "the last holder";
            spares.give(first);

            // The next take is the object just given back, and nothing was made for it.
            EXPECT_EQ(&spares.take(valued(2)), &first);
            EXPECT_EQ(spares.size(), 1u);

            // A second object while the first is out is made, not shared.
            Reading& second = spares.take(valued(3));
            EXPECT_NE(&second, &first);
            EXPECT_EQ(spares.size(), 2u);
        }

        /// **Making a thousand objects copies the spare list a handful of times, and giving them all
        /// back allocates nothing**: the list's room follows the list of all's, which doubles,
        /// where one more each time copied the whole list at every object made. A thousand
        /// takes make a thousand objects and some ten rooms for each of the two lists, nowhere near
        /// the two thousand a reserve of one more each made. The same for a value given back to a
        /// `Recycled` that made it.
        TEST(RtxSparesTest, aThousandObjectsGrowTheListsByDoublingAndGoBackFree)
        {
            Spares<Reading> spares;
            std::vector<Reading*> taken;
            taken.reserve(1000);
            EXPECT_LT(Testing::allocationsIn([&] {
                for (std::int32_t at = 0; at < 1000; ++at)
                    taken.push_back(&spares.take(valued(at)));
            }),
                1000u + 40u)
                << "a room made for every object made";

            EXPECT_EQ(Testing::allocationsIn([&] {
                for (Reading* reading : taken)
                    spares.give(*reading);
            }),
                0u)
                << "a give-back reached the heap";

            Recycled<std::vector<std::int32_t>> recycled;
            std::vector<std::vector<std::int32_t>> out;
            out.reserve(100);
            for (std::int32_t at = 0; at < 100; ++at)
                out.push_back(recycled.take());
            EXPECT_EQ(Testing::allocationsIn([&] {
                for (std::vector<std::int32_t>& value : out)
                    recycled.give(std::move(value));
            }),
                0u)
                << "a recycled value's give-back reached the heap";
        }

        /// **A fill that throws leaves nothing taken.** The pool empties the object and has it
        /// back before the throw goes on, so the next take answers with the same object, emptied,
        /// and the pool has made nothing for it. What `CellReader::readModel` lost one model per
        /// cell to: a spare taken before a walk that threw, and never filed or given back.
        TEST(RtxSparesTest, aFillThatThrowsLeavesTheObjectSpareAndEmpty)
        {
            Spares<Reading> spares;

            Reading* attempted = nullptr;
            EXPECT_THROW(spares.take([&](Reading& reading) {
                attempted = &reading;
                reading.mValue = 7;
                throw std::runtime_error("a walk that refused the file");
            }),
                std::runtime_error);
            ASSERT_NE(attempted, nullptr);
            EXPECT_EQ(spares.size(), 1u) << "one object was made for the attempt";

            Reading& taken = spares.take(valued(0));
            EXPECT_EQ(&taken, attempted) << "the object the fill threw out of is the spare";
            EXPECT_EQ(taken.mValue, 0) << "emptied on the way back";
            EXPECT_EQ(taken.mLent, 0u);
            EXPECT_EQ(spares.size(), 1u) << "nothing was made for the second take";
        }

        /// **An object given back twice is the pool's assert**, and not two entries on the spare
        /// list handing one object to two takers. Emptied before each give, as every give-back
        /// empties: the flag is the pool's, and a reuse that reset it would hide the second give.
        TEST(RtxSparesTest, anObjectGivenBackTwiceDies)
        {
            Spares<Reading> spares;
            Reading& reading = spares.take(valued(1));
            reading.reuse();
            spares.give(reading);
            reading.reuse();
            Testing::expectAssertDies([&] { spares.give(reading); }, "an object given back twice");
        }
    }
}
