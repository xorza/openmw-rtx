#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <components/rtx/renderer/memoryreport.hpp>

namespace Rtx
{
    namespace
    {
        /// A budget the driver would not state is left out of the line rather than printed as none.
        ///
        /// **Zero and "would not say" are different answers**, and a reader who cannot tell them
        /// apart reads a card with no budget extension as a card with no memory left.
        TEST(RtxMemoryReportTest, aHeapWithNoBudgetLeavesTheBudgetColumnsOut)
        {
            // A card with video memory of its own: its video memory, the system's, and a window of the
            // first.
            MemoryReport report;
            report.mHeapCount = 3;
            report.mHeaps[0] = HeapUse{ .mSize = 8ull << 30,
                .mReserved = 512ull << 20,
                .mLive = 256ull << 20,
                .mBlocks = 8,
                .mDeviceLocal = true };
            report.mHeaps[1] = HeapUse{ .mSize = 32ull << 30 };
            report.mHeaps[2] = HeapUse{ .mSize = 256ull << 20,
                .mBudget = 240ull << 20,
                .mHeld = 100ull << 20,
                .mReserved = 128ull << 20,
                .mLive = 120ull << 20,
                .mBlocks = 4,
                .mDeviceLocal = true };

            const std::string out = describeMemory(report);

            EXPECT_EQ(out.find("budget"), out.rfind("budget")) << "the heap with no budget printed one";
            EXPECT_EQ(std::count(out.begin(), out.end(), '\n'), 3) << "a line a heap and no other";
            EXPECT_EQ(out.find("  heap  0  device    8192.0 MiB   reserved   512.0   live   256.0   8 blocks\n"), 0u)
                << out;
            EXPECT_NE(out.find("  heap  1  system   32768.0 MiB"), std::string::npos)
                << "the system's heap read as video memory: " << out;
            EXPECT_NE(out.find("  heap  2  device     256.0 MiB   reserved   128.0   live   120.0   budget   240.0   "
                               "held   100.0   4 blocks\n"),
                std::string::npos)
                << out;
        }

        /// **The video heap by use, under the heaps, where the renderer says**: what the frame's
        /// targets hold beside what is kept for their largest mode, and what each kind of content
        /// holds beside what it may still take. A report with no uses prints no such line, as the
        /// one above shows by its count.
        TEST(RtxMemoryReportTest, theUsesLineSaysWhatTheVideoHeapHoldsByUse)
        {
            MemoryReport report;
            report.mHeapCount = 1;
            report.mHeaps[0] = HeapUse{ .mSize = 16ull << 30, .mDeviceLocal = true };
            report.mUses = MemoryUses{ .mFrame = 6000ull << 20,
                .mFrameReserve = 6500ull << 20,
                .mEssential = 820ull << 20,
                .mStructures = 174ull << 20,
                .mStructureRoom = 4096ull << 20,
                .mTextures = 1290ull << 20,
                .mTextureRoom = 3840ull << 20 };

            const std::string out = describeMemory(report);
            EXPECT_NE(
                out.find("  uses  frame 6000.0 of 6500.0 kept   essential 820.0   structures 174.0, room 4096.0   "
                         "textures 1290.0, room 3840.0\n"),
                std::string::npos)
                << out;
            EXPECT_EQ(std::count(out.begin(), out.end(), '\n'), 2) << "the heap and the uses";
        }

        /// **Every heap's columns read down the page**, whatever its kind and however many heaps: a
        /// column counted by hand held only while the index was one digit, and a card with ten heaps
        /// or more put every `reserved` one place to the right.
        TEST(RtxMemoryReportTest, everyHeapsColumnsReadDownThePage)
        {
            MemoryReport report;
            report.mHeapCount = 11;
            for (std::uint32_t heap = 0; heap < report.mHeapCount; ++heap)
                report.mHeaps[heap] = HeapUse{ .mSize = 256ull << 20, .mDeviceLocal = heap % 2 == 0 };

            // "  heap " 7, the index 2, "  " 2, the kind 6, "  " 2, the size 8, " MiB" 4, "   " 3:
            // `reserved` at 34 on every line.
            const std::string out = describeMemory(report);
            std::size_t lines = 0;
            for (std::size_t start = 0; start < out.size(); ++lines)
            {
                const std::size_t end = out.find('\n', start);
                EXPECT_EQ(out.find("reserved", start) - start, 34u) << out.substr(start, end - start);
                start = end + 1;
            }
            EXPECT_EQ(lines, 11u) << out;
        }
    }
}
