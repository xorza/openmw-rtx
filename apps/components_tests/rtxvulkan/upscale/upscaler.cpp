#include <string>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtxvulkan/upscale/upscaler.hpp>

namespace Rtx
{
    namespace
    {
        using RtxUpscalerTest = Testing::DeviceTest;

        /// **A renderer with no upscaler refuses one by name**, rather than tracing at the output size
        /// and letting a run believe it was upscaled, and says so to `info`.
        TEST_F(RtxUpscalerTest, aRendererWithNoUpscalerRefusesOneByName)
        {
            static_assert(!sUpscalerBuilt, "an upscaler was built in, and this test says there is none");

            EXPECT_THROW(makeUpscaler(getDevice(), mHarness.mInstance->getHandle()), Unsupported);
            EXPECT_EQ(describeUpscaling(getDevice(), mHarness.mInstance->getHandle()), "none");
        }
    }
}
