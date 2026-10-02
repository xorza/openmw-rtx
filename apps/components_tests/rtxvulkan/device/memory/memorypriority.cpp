#include <gtest/gtest.h>

#include <components/rtxvulkan/device/memory/memory.hpp>

namespace Rtx
{
    namespace
    {
        /// **Under pressure the frame's own memory goes last**: every use of content stands below
        /// essential memory, and essential memory stands at the half the library gives every block
        /// outside a pool, so a target with an allocation of its own is not below a table in a block.
        /// A dedicated essential allocation once took the request's nought.
        TEST(RtxMemoryPriorityTest, essentialMemoryStandsAboveEveryUseOfContent)
        {
            EXPECT_EQ(memoryPriorityOf(MemoryUse::Essential), 0.5f);
            EXPECT_EQ(memoryPriorityOf(MemoryUse::Structure), 0.25f);
            EXPECT_EQ(memoryPriorityOf(MemoryUse::Texture), 0.25f);
            for (const MemoryUse content : { MemoryUse::Structure, MemoryUse::Texture })
                EXPECT_GT(memoryPriorityOf(MemoryUse::Essential), memoryPriorityOf(content));
        }
    }
}
