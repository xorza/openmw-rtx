#include <chrono>
#include <cstdint>

#include <gtest/gtest.h>

#include <apps/openmw/mwrender/framesaside.hpp>

namespace MWRender
{
    namespace
    {
        using std::chrono::milliseconds;

        FramesAside::TimePoint at(const std::int32_t ms)
        {
            return FramesAside::TimePoint(milliseconds(ms));
        }

        /// **A loop frame that drew nothing before the next opened stood hidden**, from its opening to the next's;
        /// the frame the world arrives in is no hidden one, and the count starts again at every world frame. Loops
        /// opened at 0, 5 and 12 ms with the world arriving in the third: 0 to 12 hidden, 12 ms.
        TEST(MWRenderFramesAsideTest, aLoopFrameThatDrewNothingStoodHidden)
        {
            FramesAside aside;
            aside.openLoop(at(0));
            aside.openLoop(at(5));
            aside.openLoop(at(12));
            const AsideSpan hidden = aside.takeAtWorldFrame();
            EXPECT_EQ(hidden.mHidden, milliseconds(12));
            EXPECT_EQ(hidden.mInterface, milliseconds(0));

            aside.openLoop(at(20));
            const AsideSpan none = aside.takeAtWorldFrame();
            EXPECT_EQ(none.mHidden, milliseconds(0)) << "the loop frame that drew the world stood hidden";
            EXPECT_EQ(none.mInterface, milliseconds(0));
        }

        /// **The interface's frames count from the first opened to the last ended**, the game's waits between them
        /// included, less those that stood hidden, which count as hidden; and a loop frame that opened an
        /// interface frame is no hidden one. Frames at 10–14, 30–33 (hidden) and 50–52 ms: 10 to 52 is 42, less
        /// the hidden 3 is 39 of the interface's, 3 hidden.
        TEST(MWRenderFramesAsideTest, theInterfacesFramesCountFromTheFirstToTheLast)
        {
            FramesAside aside;
            aside.openLoop(at(0));
            aside.openNested(at(10));
            aside.endNested(at(14), FramesAside::NestedEnd::Drawn);
            aside.openNested(at(30));
            aside.endNested(at(33), FramesAside::NestedEnd::Hidden);
            aside.openNested(at(50));
            aside.endNested(at(52), FramesAside::NestedEnd::Drawn);
            aside.openLoop(at(60));
            const AsideSpan span = aside.takeAtWorldFrame();
            EXPECT_EQ(span.mInterface, milliseconds(39));
            EXPECT_EQ(span.mHidden, milliseconds(3)) << "the loop that drew the interface stood hidden";

            aside.openLoop(at(70));
            aside.openNested(at(71));
            aside.endNested(at(72), FramesAside::NestedEnd::Drawn);
            const AsideSpan again = aside.takeAtWorldFrame();
            EXPECT_EQ(again.mInterface, milliseconds(1)) << "the last world frame's span was carried";
            EXPECT_EQ(again.mHidden, milliseconds(0));
        }
    }
}
