#include <optional>
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

    /// **A name this user left is made afresh, as noughts, and the other side's opening is seen
    /// where the system can tell**: a POSIX name is given up by the side that opens it, so the side
    /// that made it finds it gone. Memory a side opened says nothing either way.
    TEST(PlatformSharedMemoryTest, aNameLeftIsMadeAfreshAndAnOpeningIsSeen)
    {
        const std::string name
            = "openmw-shared-again-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed());

        Platform::SharedMemory left = Platform::SharedMemory::create(name, 64);
        ASSERT_NE(left.data(), nullptr);
        static_cast<unsigned char*>(left.data())[0] = 0xAB;

        Platform::SharedMemory again = Platform::SharedMemory::create(name, 64);
        ASSERT_NE(again.data(), nullptr) << "a name this user left was not made again";
        EXPECT_EQ(static_cast<const unsigned char*>(again.data())[0], 0) << "made over what was left, as it stood";

        const std::optional<bool> before = again.isOpenedElsewhere();
        const Platform::SharedMemory opened = Platform::SharedMemory::open(name, 64);
        ASSERT_NE(opened.data(), nullptr);
        if (before.has_value())
        {
            EXPECT_FALSE(*before) << "opened before anything opened it";
            EXPECT_EQ(again.isOpenedElsewhere(), std::optional<bool>(true));
        }
        EXPECT_FALSE(opened.isOpenedElsewhere().has_value()) << "memory this side opened says who opened it";
    }
}
