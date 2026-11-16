#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <components/platform/glibctext.hpp>
#include <components/platform/heap.hpp>

namespace Platform
{
    namespace
    {
        /// Two arenas and one chunk mapped on its own, as glibc writes `malloc_info`: the main arena took 135168 bytes
        /// and keeps 64 in its fast bins and 1000 in its other free chunks, its top among them; a thread's took 270336
        /// and keeps none in fast bins, whose line glibc 2.44 leaves out, and 200000 else; the heap mapped 65540096
        /// bytes of a block on its own.
        constexpr std::string_view sReport = R"(<malloc version="1">
<heap nr="0">
<sizes>
  <size from="17" to="32" total="64" count="2"/>
  <unsorted from="1001" to="1001" total="1001" count="1"/>
</sizes>
<total type="fast" count="2" size="64"/>
<total type="rest" count="2" size="1000"/>
<system type="current" size="135168"/>
<system type="max" size="135168"/>
<aspace type="total" size="135168"/>
<aspace type="mprotect" size="135168"/>
</heap>
<heap nr="1">
<sizes>
</sizes>
<total type="rest" count="1" size="200000"/>
<system type="current" size="270336"/>
<system type="max" size="270336"/>
<aspace type="total" size="270336"/>
<aspace type="mprotect" size="270336"/>
<aspace type="subheaps" size="1"/>
</heap>
<total type="fast" count="2" size="64"/>
<total type="rest" count="3" size="201000"/>
<total type="mmap" count="1" size="65540096"/>
<system type="current" size="405504"/>
<system type="max" size="405504"/>
<aspace type="total" size="405504"/>
<aspace type="mprotect" size="405504"/>
</malloc>
)";

        /// **Each arena's free is its fast bins' and its other free chunks', and the heap's in use is everything taken
        /// but that**: the main arena 64 + 1000 = 1064 free of 135168, the thread's 200000 of 270336; the heap took
        /// 135168 + 270336 + 65540096 = 65945600, of which 65945600 - 1064 - 200000 = 65744536 is in use.
        TEST(PlatformGlibcTextTest, eachArenasFreeIsItsFreeChunksAndTheHeapsInUseTheRest)
        {
            const std::optional<HeapHeld> held = GlibcText::heapHeld(sReport);
            ASSERT_TRUE(held.has_value());
            EXPECT_EQ(held->mTaken, 65945600u);
            EXPECT_EQ(held->mInUse, 65744536u);
            ASSERT_EQ(held->mArenas.size(), 2u);
            EXPECT_EQ(held->mArenas[0].mTaken, 135168u);
            EXPECT_EQ(held->mArenas[0].mFree, 1064u);
            EXPECT_EQ(held->mArenas[1].mTaken, 270336u);
            EXPECT_EQ(held->mArenas[1].mFree, 200000u);
        }

        /// **A report it cannot read whole says nothing**: no arena, an arena's taken missing or a size not a number,
        /// an arena cut off, more free than taken, or no line of the chunks mapped on their own.
        TEST(PlatformGlibcTextTest, aReportItCannotReadWholeSaysNothing)
        {
            const auto replaced = [](std::string_view from, std::string_view to) {
                std::string report(sReport);
                const std::size_t at = report.find(from);
                EXPECT_NE(at, std::string::npos) << from;
                report.replace(at, from.size(), to);
                return report;
            };
            EXPECT_FALSE(GlibcText::heapHeld("").has_value());
            EXPECT_FALSE(GlibcText::heapHeld("<malloc version=\"1\">\n</malloc>\n").has_value());
            EXPECT_FALSE(GlibcText::heapHeld(replaced("<system type=\"current\" size=\"270336\"/>\n", "")).has_value())
                << "an arena with no line of what it took";
            EXPECT_FALSE(GlibcText::heapHeld(replaced("size=\"64\"", "size=\"6x\"")).has_value())
                << "fast bins' size that is not a number";
            EXPECT_FALSE(GlibcText::heapHeld(
                replaced("size=\"270336\"/>\n<system type=\"max\"", "size=\"x\"/>\n<system type=\"max\""))
                    .has_value())
                << "an arena's taken that is not a number";
            EXPECT_FALSE(GlibcText::heapHeld(replaced("size=\"200000\"", "size=\"300000\"")).has_value())
                << "more free than taken";
            EXPECT_FALSE(
                GlibcText::heapHeld(replaced("<total type=\"mmap\" count=\"1\" size=\"65540096\"/>\n", "")).has_value())
                << "no chunks mapped on their own";
            EXPECT_FALSE(GlibcText::heapHeld(sReport.substr(0, sReport.find("</heap>\n<total"))).has_value())
                << "an arena cut off";
        }
    }
}
