#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <components/platform/sharedmemory.hpp>

namespace
{
    /// **A move assignment unmaps what it held before it takes the other's**: the memory the
    /// target made is gone once the assignment returns, where a swap left it mapped in the source
    /// for as long as the source stood.
    TEST(PlatformSharedMemoryTest, aMoveAssignmentUnmapsWhatItHeldThenTakesTheOther)
    {
        const std::string seed = std::to_string(::testing::UnitTest::GetInstance()->random_seed());
        const std::string first = "openmw-shared-first-" + seed;
        const std::string second = "openmw-shared-second-" + seed;

        Platform::SharedMemory target = Platform::SharedMemory::create(first, 64);
        Platform::SharedMemory source = Platform::SharedMemory::create(second, 64);
        ASSERT_NE(target.data(), nullptr);
        ASSERT_NE(source.data(), nullptr);
        void* const taken = source.data();

        target = std::move(source);
        EXPECT_EQ(target.data(), taken);
        EXPECT_EQ(source.data(), nullptr);
        EXPECT_EQ(Platform::SharedMemory::open(first, 64).data(), nullptr) << "the memory given up is still there";
    }
}
