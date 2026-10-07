#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
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
        /// were fourteen images for nothing. A new width or a new height is a new chain, and every
        /// chain stores its radiance at the width it was made with.
        TEST_F(RtxTraceChainTest, aResizeToTheExtentItStandsAtRebuildsNothing)
        {
            for (const RadianceWidth radiance : { RadianceWidth::Summed, RadianceWidth::Shown })
            {
                TraceChain chain(mRenderer.getDevice(), mRenderer.getTracePasses(), 1, radiance);

                chain.resize(64, 32);
                const GBuffer* const built = &chain.getChannels();
                EXPECT_EQ(chain.getColour().getFormat(), radianceFormat(radiance));
                chain.resize(64, 32);
                EXPECT_EQ(&chain.getChannels(), built) << "the same extent made the channels anew";

                chain.resize(48, 32);
                EXPECT_NE(&chain.getChannels(), built) << "another width kept the channels";
                EXPECT_EQ(chain.getWidth(), 48u);
                EXPECT_EQ(chain.getHeight(), 32u);
                EXPECT_EQ(chain.getColour().getFormat(), radianceFormat(radiance)) << "a resize changed the width";

                const GBuffer* const narrower = &chain.getChannels();
                chain.resize(48, 16);
                EXPECT_NE(&chain.getChannels(), narrower) << "another height kept the channels";
            }

            EXPECT_NE(radianceFormat(RadianceWidth::Summed), radianceFormat(RadianceWidth::Shown))
                << "the two widths store alike, so the format says nothing of which the chain took";
        }
    }
}
