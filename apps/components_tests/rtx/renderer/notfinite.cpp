#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <components/rtx/renderer/renderer.hpp>

namespace Rtx
{
    namespace
    {
        /// **A module is one entry however often it is added, in the order it first came**, and a
        /// count of nought adds none: the census reads every module's word every frame, and a stop
        /// that summed sixty frames of nought would otherwise list every module the device made.
        ///
        /// Two frames: the first adds 3 against the scatter, nought against the wavelet and 5
        /// against the integration; the second 2 against the integration and 1 against the
        /// composite. The stop holds 3, 5 + 2 = 7 and 1, in that order, 11 in all.
        TEST(RtxNotFiniteTest, aModuleIsOneEntryInTheOrderItFirstCame)
        {
            // Spelled apart from the frames' names, as the census's own strings are from a report's.
            const std::string scatter = "fogscatter.rgen";

            NotFinite first;
            first.add("fogscatter.rgen", 3);
            first.add("atrous.comp", 0);
            first.add("fogintegrate.comp", 5);

            NotFinite second;
            second.add("fogintegrate.comp", 2);
            second.add("composite.comp", 1);

            NotFinite stop;
            EXPECT_EQ(stop.total(), 0u);
            EXPECT_TRUE(stop.kernels().empty());

            stop.add(first);
            stop.add(second);

            ASSERT_EQ(stop.kernels().size(), 3u);
            EXPECT_EQ(stop.kernels()[0].mKernel, scatter);
            EXPECT_EQ(stop.kernels()[0].mStores, 3u);
            EXPECT_EQ(stop.kernels()[1].mKernel, "fogintegrate.comp");
            EXPECT_EQ(stop.kernels()[1].mStores, 7u);
            EXPECT_EQ(stop.kernels()[2].mKernel, "composite.comp");
            EXPECT_EQ(stop.kernels()[2].mStores, 1u);

            EXPECT_EQ(stop.total(), 11u);
            EXPECT_EQ(stop.of(scatter), 3u);
            EXPECT_EQ(stop.of("atrous.comp"), 0u) << "added as nought, so never held";
            EXPECT_EQ(stop.of("tone.comp"), 0u);
        }
    }
}
