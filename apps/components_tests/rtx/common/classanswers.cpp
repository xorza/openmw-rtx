#include <cstdint>

#include <gtest/gtest.h>

#include <components/rtx/common/classanswers.hpp>

namespace Rtx
{
    namespace
    {
        struct Base
        {
            virtual ~Base() = default;
        };
        struct First : Base
        {
        };
        struct Second : Base
        {
        };
        struct Third : Base
        {
        };

        /// **A class is learned once while the table has room, and at every ask past it**: two kept classes asked
        /// twice each learn twice, and a third past a table of two learns at each of its two asks, answering alike.
        TEST(RtxClassAnswersTest, aClassIsLearnedOnceWhileTheTableHasRoomAndAtEveryAskPastIt)
        {
            ClassAnswers<std::int32_t, 2> answers;
            std::int32_t learned = 0;
            const auto ask = [&](const Base& object, const std::int32_t answer) {
                return answers.of(typeid(object), [&] {
                    ++learned;
                    return answer;
                });
            };

            const First first;
            const Second second;
            const Third third;
            EXPECT_EQ(ask(first, 1), 1);
            EXPECT_EQ(ask(second, 2), 2);
            EXPECT_EQ(ask(first, 9), 1) << "a kept class answered other than it was learned as";
            EXPECT_EQ(ask(second, 9), 2);
            EXPECT_EQ(learned, 2);

            EXPECT_EQ(ask(third, 3), 3);
            EXPECT_EQ(ask(third, 3), 3);
            EXPECT_EQ(learned, 4) << "a class past the table was kept";
            EXPECT_EQ(ask(first, 9), 1) << "a class past the table took a kept one's place";
        }
    }
}
