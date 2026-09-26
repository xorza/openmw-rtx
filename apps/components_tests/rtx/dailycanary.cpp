#include <climits>

#include <gtest/gtest.h>

namespace
{
    // TEMPORARY: a bug only the daily sanitizer run can see, to prove it fails and files an issue.
    // Removed once it has.
    TEST(RtxDailyCanaryTest, aSignedOverflowOnlyTheSanitizerSees)
    {
        volatile int largest = INT_MAX;
        volatile int past = largest + 1;
        (void)past;
    }
}
