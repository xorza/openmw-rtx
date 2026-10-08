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
            // A card without resizable BAR: video memory, the system's, and the window.
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
                .mDeviceLocal = true,
                .mHostVisible = true };

            report.mHostWrittenReserved = 128ull << 20;
            report.mHostWrittenLive = 120ull << 20;

            const std::string out = describeMemory(report);

            EXPECT_EQ(out.find("budget"), out.rfind("budget")) << "the heap with no budget printed one";

            // **The line that answers the Turing question**, and the only one that can on a card
            // whose one video memory heap is host-visible throughout.
            EXPECT_NE(out.find("host-written"), std::string::npos);
            EXPECT_EQ(std::count(out.begin(), out.end(), '\n'), 4) << "a line a heap, and the one below them";
            EXPECT_NE(out.find("device-only"), std::string::npos);
            EXPECT_NE(out.find("system"), std::string::npos) << "the system's heap read as video memory";
            EXPECT_NE(out.find("  heap  2  device+host "), std::string::npos) << out;
            EXPECT_NE(out.find("8192.0 MiB"), std::string::npos) << "the first heap's size";
            EXPECT_NE(out.find("256.0 MiB"), std::string::npos) << "the window's size";
            EXPECT_EQ(out.substr(0, 2), "  ") << "the lines were not indented as asked";
        }

        /// **The video heap by use, under the heaps, where the renderer says**: what the frame's
        /// targets hold beside what is kept for their largest mode, and what each kind of content
        /// holds beside what it may still take. A report with no uses prints no such line, as the
        /// one above shows by its count.
        TEST(RtxMemoryReportTest, theUsesLineSaysWhatTheVideoHeapHoldsByUse)
        {
            MemoryReport report;
            report.mHeapCount = 1;
            report.mHeaps[0] = HeapUse{ .mSize = 16ull << 30, .mDeviceLocal = true, .mHostVisible = true };
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
            EXPECT_EQ(std::count(out.begin(), out.end(), '\n'), 3) << "the heap, the host-written line and the uses";
        }

        /// **A card with resizable BAR has one heap of video memory, which the host writes into
        /// throughout**, and its line names it as both: read by the host's flag alone, 16 GiB of
        /// video memory read as the small window of a card without it, and no line said "device".
        TEST(RtxMemoryReportTest, aHeapTheDeviceHoldsAndTheHostWritesIsNamedAsBoth)
        {
            MemoryReport report;
            report.mHeapCount = 2;
            report.mHeaps[0] = HeapUse{ .mSize = 16ull << 30, .mDeviceLocal = true, .mHostVisible = true };
            report.mHeaps[1] = HeapUse{ .mSize = 32ull << 30 };

            const std::string out = describeMemory(report);
            EXPECT_EQ(out.find("  heap  0  device+host    16384.0 MiB"), 0u) << out;
            EXPECT_NE(out.find("  heap  1  system         32768.0 MiB"), std::string::npos) << out;
            EXPECT_EQ(out.find("device-only"), std::string::npos) << out;
        }

        /// **The line under the heaps reads down the page with them**, whatever the number of
        /// heaps: a column counted by hand from the heap line's format held only while the index
        /// was one digit, and a card with ten heaps or more put every `reserved` one place to the
        /// right of the host-written line's.
        TEST(RtxMemoryReportTest, theHostWrittenLineKeepsTheHeapsColumns)
        {
            MemoryReport report;
            report.mHeapCount = 11;
            for (std::uint32_t heap = 0; heap < report.mHeapCount; ++heap)
                report.mHeaps[heap] = HeapUse{ .mSize = 256ull << 20 };

            // "  heap " 7, the index 2, "  " 2, the kind 12, "  " 2, the size 8, " MiB" 4, "   " 3:
            // `reserved` at 40 on every line.
            const std::string out = describeMemory(report);
            std::size_t lines = 0;
            for (std::size_t start = 0; start < out.size(); ++lines)
            {
                const std::size_t end = out.find('\n', start);
                EXPECT_EQ(out.find("reserved", start) - start, 40u) << out.substr(start, end - start);
                start = end + 1;
            }
            EXPECT_EQ(lines, 12u) << out;
        }
    }
}
