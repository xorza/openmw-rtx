#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>
#include <components/rtxvulkan/trace/tracechain.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx
{
    namespace
    {
        using RtxTraceChainTest = Testing::RendererTest;

        /// **A chain asked for the extent it stands at keeps what it holds**: an upscaling mode
        /// changed between two that trace at one size asks again, and the channels made anew for it
        /// were fourteen images for nothing. A new width, a new height or a new radiance width is a
        /// new chain.
        TEST_F(RtxTraceChainTest, aResizeToTheExtentItStandsAtRebuildsNothing)
        {
            TraceChain chain(mRenderer.getDevice(), mRenderer.getTracePasses(), 1);

            chain.resize(64, 32, RadianceWidth::Summed);
            const GBuffer* const built = &chain.getChannels();
            chain.resize(64, 32, RadianceWidth::Summed);
            EXPECT_EQ(&chain.getChannels(), built) << "the same extent made the channels anew";

            chain.resize(64, 32, RadianceWidth::Shown);
            EXPECT_NE(&chain.getChannels(), built) << "another radiance width kept the channels";

            const GBuffer* const shown = &chain.getChannels();
            chain.resize(48, 32, RadianceWidth::Shown);
            EXPECT_NE(&chain.getChannels(), shown) << "another width kept the channels";
            EXPECT_EQ(chain.getWidth(), 48u);
            EXPECT_EQ(chain.getHeight(), 32u);
        }
    }
}
