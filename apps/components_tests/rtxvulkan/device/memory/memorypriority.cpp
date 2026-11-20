#include <gtest/gtest.h>

#include <components/rtxvulkan/device/memory/memory.hpp>

namespace Rtx
{
    namespace
    {
        /// **Under pressure the frame's own memory goes last**: the content arena stands below the frame's and
        /// essential memory, which stand at the half the library gives every block outside a pool, so a target with an
        /// allocation of its own is not below a table in a block, and does not take the nought a request with no
        /// priority states.
        TEST(RtxMemoryPriorityTest, essentialMemoryStandsAboveContent)
        {
            EXPECT_EQ(memoryPriorityOf(MemoryUse::Frame), 0.5f);
            EXPECT_EQ(memoryPriorityOf(MemoryUse::Essential), 0.5f);
            EXPECT_EQ(memoryPriorityOf(MemoryUse::Content), 0.25f);
        }
    }
}
