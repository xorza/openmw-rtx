#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/pipelinecache.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/pipeline/shadercode.hpp>
#include <components/rtxvulkan/pipeline/tracepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/probe.h>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        /// Where each invocation writes what its launch index was.
        constexpr std::array<VkDescriptorSetLayoutBinding, 1> sBindings{
            VkDescriptorSetLayoutBinding{ Shaders::PROBE_BIND_LAUNCHED, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr },
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
        /// **The shader binding table and `vkCmdTraceRaysKHR`, asked of the device directly.** Neither says anything
        /// when it is wrong: a handle at the wrong offset, or a launch of the wrong grid, is a missing or garbled
        /// picture and no error. Every slot is filled with a value that cannot occur, so an invocation that never ran
        /// and one that ran twice are different failures.
        TEST_F(RtxTracePipelineTest, aLaunchRunsOnceAtEveryIndex)
        {
            const Device& device = getDevice();

            constexpr std::array<std::string_view, 1> modules{ "traceprobe.rgen.spv" };
            const ShaderCode code(device, modules);
            const TracePipeline<NoConstants> pipeline(
                device, sBindings, {}, code, "traceprobe.rgen.spv", "trace probe");
            EXPECT_EQ(pipeline.getPushRange().size, 0u) << "a pipeline pushed nothing declared a range";

            constexpr std::uint32_t sCount = sWidth * sHeight;
            const Buffer written
                = Buffer::readBack(device, sCount * sizeof(Launched), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "test");

            constexpr std::uint32_t sUnwritten = 0xFFFFFFFFu;
            std::memset(written.map(), 0xFF, sCount * sizeof(Launched));

            DescriptorWrites write(pipeline);
            write.buffer(Shaders::PROBE_BIND_LAUNCHED, VkDescriptorBufferInfo{ written.getHandle(), 0, VK_WHOLE_SIZE });

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

        /// **A launch's code is asked of its text forms alone**, and a form its driver will not give whole ends no
        /// run (`Device::writeCode`): AMD's Windows driver answered `VK_INCOMPLETE` for a launch's binary forms
        /// whatever room they were given, which ended a run asked for its pipelines' code. A device of its own,
        /// writing its code to a folder; every file it writes stands under a form's heading.
        TEST_F(RtxTracePipelineTest, aLaunchsCodeIsAskedOfItsTextFormsAlone)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "code";
            const Instance& instance = *mHarness.mInstance;
            const Device device(instance, PhysicalDevice::select(instance.getHandle(), {}), Testing::getCensusShaders(),
                PipelineCacheSpec{}, DeviceReport{ .mCodeFolder = folder });

            constexpr std::array<std::string_view, 1> modules{ "traceprobe.rgen.spv" };
            const ShaderCode code(device, modules);
            EXPECT_NO_THROW(
                (TracePipeline<NoConstants>(device, sBindings, {}, code, "traceprobe.rgen.spv", "trace probe")));

            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
            {
                std::ifstream file(entry.path());
                const std::string written(std::istreambuf_iterator<char>(file), {});
                EXPECT_TRUE(written.starts_with("## ")) << entry.path() << " stands under no form's heading";
            }
        }
    }
}
