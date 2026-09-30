#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/pipeline/tracepipeline.hpp>

namespace Rtx
{
    namespace
    {
        /// Where each invocation writes what its launch index was.
        constexpr std::array<VkDescriptorSetLayoutBinding, 1> sBindings{
            VkDescriptorSetLayoutBinding{
                0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr },
        };

        /// A grid that is neither square nor a multiple of a warp, so a launch that rounded its
        /// extent up or laid its rows out at the wrong stride is a failure rather than a coincidence.
        constexpr std::uint32_t sWidth = 37;
        constexpr std::uint32_t sHeight = 11;

        /// One launch index, as the shader writes it.
        struct Launched
        {
            std::uint32_t mX;
            std::uint32_t mY;
        };

        struct RtxTracePipelineTest : Testing::DeviceTest
        {
        };

        /// A launch runs once at every index of the grid it was given.
        ///
        /// **The shader binding table and `vkCmdTraceRaysKHR`, asked of the device directly.** The
        /// trace rests on both and neither says anything when it is wrong: a table whose one handle
        /// landed at the wrong offset, or a launch of the wrong grid, is a picture that is missing
        /// or garbled rather than an error anything reports. Every slot is filled with a value that
        /// cannot occur, so an invocation that never ran and one that ran twice are different
        /// failures.
        TEST_F(RtxTracePipelineTest, aLaunchRunsOnceAtEveryIndex)
        {
            const Device& device = getDevice();

            const TraceShaders shaders{
                .mRaygen = "traceprobe.rgen.spv",
            };
            const TracePipeline<NoConstants> pipeline(device, sBindings, {}, shaders, "trace probe");
            EXPECT_EQ(pipeline.getPushRange().size, 0u) << "a pipeline pushed nothing declared a range";

            constexpr std::uint32_t sCount = sWidth * sHeight;
            const Buffer written
                = Buffer::readBack(device, sCount * sizeof(Launched), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "test");

            constexpr std::uint32_t sUnwritten = 0xFFFFFFFFu;
            std::memset(written.map(), 0xFF, sCount * sizeof(Launched));

            DescriptorWrites write(pipeline);
            write.buffer(0, VkDescriptorBufferInfo{ written.getHandle(), 0, VK_WHOLE_SIZE });

            EXPECT_EQ(pipeline.getTable().getNamedUntil(), 0u) << "the table was named before any launch";
            getPool().submitAndWait([&](VkCommandBuffer commands) {
                bind(commands, pipeline);
                pushDescriptors(commands, pipeline, write);
                pipeline.traceRays(commands, sWidth, sHeight);

                // The launch reads the table by address, so the launch is what names it.
                EXPECT_EQ(pipeline.getTable().getNamedUntil(), device.getTimeline().getNext())
                    << "a launch did not name the table it reads";

                written.orderForHostRead(commands);
            });

            std::vector<Launched> read(sCount);
            std::memcpy(read.data(), written.map(), read.size() * sizeof(Launched));

            for (std::uint32_t y = 0; y < sHeight; ++y)
                for (std::uint32_t x = 0; x < sWidth; ++x)
                {
                    const Launched& at = read[y * sWidth + x];
                    if (at.mX == sUnwritten && at.mY == sUnwritten)
                    {
                        ADD_FAILURE() << "no invocation wrote " << x << ", " << y;
                        continue;
                    }

                    EXPECT_EQ(at.mX, x) << "the invocation at " << x << ", " << y << " reported column " << at.mX;
                    EXPECT_EQ(at.mY, y) << "the invocation at " << x << ", " << y << " reported row " << at.mY;
                }
        }
    }
}
