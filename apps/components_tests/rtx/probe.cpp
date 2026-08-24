#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <components/rtx/shaders/probe.h>
#include <components/rtxvulkan/buffer.hpp>
#include <components/rtxvulkan/commands.hpp>
#include <components/rtxvulkan/computepipeline.hpp>
#include <components/rtxvulkan/device.hpp>
#include <components/rtxvulkan/hostbuffer.hpp>

#include "harness.hpp"

namespace Rtx
{
    namespace
    {
        /// The source in, both readings out.
        constexpr std::array<VkDescriptorSetLayoutBinding, 2> sBindings{
            VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        };

        /// Enough to cross the workgroup several times and end partway through one: 300 is four
        /// full groups of 64 and a tail of 44.
        constexpr std::uint32_t sCount = 300;

        /// What the device is asked to read back.
        ///
        /// Every value is a sum of powers of two, so it survives the trip exactly and an equality
        /// comparison is a statement about the hardware rather than about rounding. The three
        /// channels are pulled apart — different magnitudes, one of them negative — so a reading
        /// that swapped or shifted components cannot pass.
        std::vector<osg::Vec3f> makePattern()
        {
            std::vector<osg::Vec3f> pattern;
            pattern.reserve(sCount);
            for (std::uint32_t at = 0; at < sCount; ++at)
            {
                const auto index = static_cast<float>(at);
                pattern.emplace_back(index * 0.25f + 1.0f, -index * 0.5f - 2.0f, index * 0.125f + 0.375f);
            }

            return pattern;
        }

        /// Runs the probe over `source` and gives back the two halves it wrote, descriptor first.
        std::vector<osg::Vec3f> probe(const Device& device, const ComputePipeline& pipeline, CommandPool& pool,
            VkBuffer source, VkDeviceAddress address)
        {
            const Buffer readings(device, sizeof(osg::Vec3f) * sCount * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            const VkDescriptorBufferInfo from{ source, 0, VK_WHOLE_SIZE };
            const VkDescriptorBufferInfo into{ readings.getHandle(), 0, VK_WHOLE_SIZE };

            const auto write = [](std::uint32_t binding, const VkDescriptorBufferInfo& info) {
                return VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstBinding = binding,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .pBufferInfo = &info,
                };
            };
            const std::array<VkWriteDescriptorSet, 2> writes{ write(0, from), write(1, into) };

            const Shaders::ProbeConstants constants{ .mSource = address, .mCount = sCount };

            pool.submitAndWait([&](VkCommandBuffer commands) {
                vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getHandle());
                vkCmdPushDescriptorSet(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getLayout(), 0,
                    static_cast<std::uint32_t>(writes.size()), writes.data());
                vkCmdPushConstants(
                    commands, pipeline.getLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
                vkCmdDispatch(commands, (sCount + Shaders::PROBE_WORKGROUP - 1) / Shaders::PROBE_WORKGROUP, 1, 1);

                const VkBufferMemoryBarrier2 written{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                    .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                    .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
                    .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = readings.getHandle(),
                    .size = VK_WHOLE_SIZE,
                };
                const VkDependencyInfo dependency{
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .bufferMemoryBarrierCount = 1,
                    .pBufferMemoryBarriers = &written,
                };
                vkCmdPipelineBarrier2(commands, &dependency);
            });

            std::vector<osg::Vec3f> read(sCount * 2);
            const void* mapped = readings.map();
            std::memcpy(read.data(), mapped, read.size() * sizeof(osg::Vec3f));
            readings.unmap();

            return read;
        }

        /// Both readings of one buffer, against the pattern and against each other.
        void expectAgreement(
            const std::vector<osg::Vec3f>& pattern, const std::vector<osg::Vec3f>& read, const std::string& memory)
        {
            for (std::uint32_t at = 0; at < sCount; ++at)
            {
                EXPECT_EQ(read[at], pattern[at])
                    << "the descriptor read element " << at << " of " << memory << " memory as " << read[at].x() << ", "
                    << read[at].y() << ", " << read[at].z();
                EXPECT_EQ(read[sCount + at], pattern[at])
                    << "the pointer read element " << at << " of " << memory << " memory as " << read[sCount + at].x()
                    << ", " << read[sCount + at].y() << ", " << read[sCount + at].z() << ", where the descriptor read "
                    << read[at].x() << ", " << read[at].y() << ", " << read[at].z();
            }
        }

        /// A `buffer_reference` pointer and a storage-buffer descriptor read the same bytes.
        ///
        /// **The question that stopped the geometry blocking, asked of the device directly.** Moving
        /// the normal fetch from a descriptor to a pointer changed the picture on sixteen views and
        /// nothing else did; with nothing able to ask this, it had to be put to a whole traced frame
        /// and answered by elimination, which cost a day and reached the wrong answer twice.
        ///
        /// **Both memory kinds, because the renderer uses both.** Normals live in a `HostBuffer` —
        /// resizable-BAR video memory the host writes straight into, and write-combining — and
        /// indices and texture coordinates in ordinary device-local memory staged through a copy. A
        /// pointer read that only misbehaves in one of them would look like a shader bug.
        TEST(RtxProbeTest, aPointerAndADescriptorReadTheSameBytes)
        {
            std::string reason;
            const Testing::Harness* harness = Testing::getHarness(reason);
            if (harness == nullptr)
                GTEST_SKIP() << reason;

            const Device& device = *harness->mDevice;
            const ComputePipeline pipeline(device, sBindings, sizeof(Shaders::ProbeConstants), {},
                Testing::getShaderDirectory() / "probe.comp.spv", "probe");
            CommandPool pool(device);

            const std::vector<osg::Vec3f> pattern = makePattern();
            constexpr VkBufferUsageFlags usage
                = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

            const HostBuffer resident(device, pattern.size() * sizeof(osg::Vec3f), usage);
            resident.write(std::span<const osg::Vec3f>(pattern));
            expectAgreement(pattern, probe(device, pipeline, pool, resident.getHandle(), resident.getDeviceAddress()),
                "host-visible");

            const Buffer staged = uploadBuffer(device, pool, std::span<const osg::Vec3f>(pattern), usage);
            expectAgreement(
                pattern, probe(device, pipeline, pool, staged.getHandle(), staged.getDeviceAddress()), "device-local");
        }
    }
}
