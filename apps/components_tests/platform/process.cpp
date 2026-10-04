#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <components/platform/process.hpp>

namespace
{
    using Cpus = std::optional<std::vector<std::uint32_t>>;

    /// **A CPU list is single CPUs and ranges, both ends in, with the line break sysfs writes**, and
    /// anything else is not one: an empty list, an empty item, a range backwards, a sign, a number
    /// past the kernel's 8192.
    TEST(RtxPlatformProcessTest, aCpuListNamesItsSingleCpusAndBothEndsOfItsRanges)
    {
        EXPECT_EQ(Platform::Process::parseCpuList("0-15\n"),
            (Cpus{ { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 } }));
        EXPECT_EQ(Platform::Process::parseCpuList("0-2,8,16-17"), (Cpus{ { 0, 1, 2, 8, 16, 17 } }));
        EXPECT_EQ(Platform::Process::parseCpuList("3"), (Cpus{ { 3 } }));
        EXPECT_EQ(Platform::Process::parseCpuList("5-5"), (Cpus{ { 5 } }));
        EXPECT_EQ(Platform::Process::parseCpuList("8191"), (Cpus{ { 8191 } }));

        for (const std::string_view text :
            { "", "\n", ",", "0,", ",0", "0-", "-3", "3-1", "a", "0-x", "+1", "8192", "0-8192", "1 2" })
            EXPECT_EQ(Platform::Process::parseCpuList(text), std::nullopt) << '"' << text << '"';
    }

    /// **`malloc` is on huge pages where glibc was asked and the kernel grants it**: the tunable at
    /// one under a mode of `always` or `madvise`, which the kernel writes in brackets among the
    /// three; at two whatever the mode, since it takes reserved pages; the last setting of the name
    /// among other tunables; and neither where the tunable is absent, at nought, or at one under
    /// `never`.
    TEST(RtxPlatformProcessTest, mallocIsOnHugePagesWhereGlibcWasAskedAndTheKernelGrantsIt)
    {
        constexpr std::string_view madvise = "always [madvise] never\n";
        constexpr std::string_view always = "[always] madvise never\n";
        constexpr std::string_view never = "always madvise [never]\n";

        EXPECT_TRUE(Platform::Process::mallocOnHugePages("glibc.malloc.hugetlb=1", madvise));
        EXPECT_TRUE(Platform::Process::mallocOnHugePages("glibc.malloc.hugetlb=1", always));
        EXPECT_FALSE(Platform::Process::mallocOnHugePages("glibc.malloc.hugetlb=1", never));
        EXPECT_TRUE(Platform::Process::mallocOnHugePages("glibc.malloc.hugetlb=2", never));
        EXPECT_FALSE(Platform::Process::mallocOnHugePages("glibc.malloc.hugetlb=0", madvise));
        EXPECT_FALSE(Platform::Process::mallocOnHugePages("", madvise));
        EXPECT_FALSE(Platform::Process::mallocOnHugePages("glibc.malloc.arena_max=1", madvise));
        EXPECT_TRUE(Platform::Process::mallocOnHugePages("glibc.malloc.arena_max=1:glibc.malloc.hugetlb=1", madvise));
        EXPECT_FALSE(Platform::Process::mallocOnHugePages("glibc.malloc.hugetlb=1:glibc.malloc.hugetlb=0", madvise))
            << "the last setting is the one glibc keeps";
    }
}
