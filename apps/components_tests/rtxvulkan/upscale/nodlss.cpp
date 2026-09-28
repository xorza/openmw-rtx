#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>

namespace
{
    /// **Skipped rather than absent**, so a build with no DLSS says so instead of quietly running
    /// fewer tests.
    ///
    /// CMake builds this file in place of `dlss.cpp` where DLSS is not built. One test rather than a
    /// stub per test there, because a stub written per name is a list that stops matching the moment
    /// a test is added to the other file. Over the device fixture, as every test of this binary is
    /// (`RtxSourceTreeTest`).
    using RtxDlss = Rtx::Testing::DeviceTest;

    TEST_F(RtxDlss, thisBuildHasNoRayReconstruction)
    {
        GTEST_SKIP() << "this build has no DLSS; configure with -DOPENMW_RTX_DLSS=ON";
    }
}
