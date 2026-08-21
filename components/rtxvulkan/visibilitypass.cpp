#include "visibilitypass.hpp"

#include <array>
#include <cassert>
#include <span>

#include <components/rtx/bluenoise.hpp>

#include "buffer.hpp"
#include "commands.hpp"
#include "gbuffer.hpp"
#include "scenebuffers.hpp"

namespace Rtx
{
    namespace
    {
        std::uint32_t groupsFor(std::uint32_t extent)
        {
            return (extent + Shaders::VISIBILITY_WORKGROUP - 1) / Shaders::VISIBILITY_WORKGROUP;
        }

        constexpr auto sCompute = VK_SHADER_STAGE_COMPUTE_BIT;
        constexpr auto sStorage = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        constexpr auto sImage = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

        /// The structure, the four channels it writes, and the tables a hit reads, in the order the
        /// shader declares them. The channels are at one, fifteen, seventeen and eighteen because
        /// they grew onto the end of a layout that already existed rather than renumbering the
        /// tables under them.
        constexpr std::array<VkDescriptorSetLayoutBinding, 19> sBindings{
            VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 1, sImage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 2, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 3, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 4, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 5, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 6, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 7, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 8, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 9, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 10, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 11, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 12, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 13, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 14, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 15, sImage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 16, sStorage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 17, sImage, 1, sCompute },
            VkDescriptorSetLayoutBinding{ 18, sImage, 1, sCompute },
        };
    }

    VisibilityPass::VisibilityPass(const Device& device, CommandPool& pool,
        const std::filesystem::path& shaderDirectory, VkDescriptorSetLayout textureLayout)
        : mBlueNoise(uploadBuffer(device, pool, BlueNoise::shared().getValues(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
        , mPipeline(device, sBindings, sizeof(Shaders::VisibilityConstants), std::span(&textureLayout, 1),
              shaderDirectory / "visibility.comp.spv", "visibility")
    {
    }

    void VisibilityPass::record(VkCommandBuffer commands, const VisibilityInputs& inputs, const GBuffer& buffer,
        const Buffer& hitCount, const Shaders::VisibilityConstants& constants) const
    {
        assert(buffer.getWidth() >= constants.mWidth && buffer.getHeight() >= constants.mHeight);

        const VkWriteDescriptorSetAccelerationStructureKHR sceneWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
            .accelerationStructureCount = 1,
            .pAccelerationStructures = &inputs.mScene,
        };
        const std::array<VkDescriptorImageInfo, 4> channels{
            VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getDirect().getView(), VK_IMAGE_LAYOUT_GENERAL },
            VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getIndirect().getView(), VK_IMAGE_LAYOUT_GENERAL },
            VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getModulate().getView(), VK_IMAGE_LAYOUT_GENERAL },
            VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getGuide().getView(), VK_IMAGE_LAYOUT_GENERAL },
        };
        // In `sBindings`' order, which is where those four numbers are explained.
        constexpr std::array<std::uint32_t, 4> channelBindings{ 1, 15, 17, 18 };

        // Bindings two upwards are all storage buffers, in the order the shader declares them.
        const std::array<VkDescriptorBufferInfo, 13> buffers{
            VkDescriptorBufferInfo{ hitCount.getHandle(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getNormals(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getTexCoords(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getIndices(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getMeshes(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getInstances(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getMaterials(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getLayers(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getMasks(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getLights(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getLightOffsets(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getLightIndices(), 0, VK_WHOLE_SIZE },
            VkDescriptorBufferInfo{ inputs.mBuffers->getWaves(), 0, VK_WHOLE_SIZE },
        };
        const VkDescriptorBufferInfo noiseWrite{ mBlueNoise.getHandle(), 0, VK_WHOLE_SIZE };

        std::array<VkWriteDescriptorSet, 19> writes{};
        writes[0] = VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = &sceneWrite,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        };
        for (std::uint32_t i = 0; i < channels.size(); ++i)
            writes[1 + i] = VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = channelBindings[i],
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &channels[i],
            };
        for (std::uint32_t i = 0; i < buffers.size(); ++i)
            writes[i + 5] = VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = i + 2,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &buffers[i],
            };
        writes[18] = VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 16,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &noiseWrite,
        };

        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline.getHandle());
        vkCmdPushDescriptorSet(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline.getLayout(), 0,
            static_cast<std::uint32_t>(writes.size()), writes.data());
        vkCmdBindDescriptorSets(
            commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline.getLayout(), 1, 1, &inputs.mTextures, 0, nullptr);
        // The grid's geometry belongs to the lamps it was binned from, so it is filled here rather
        // than by whoever assembled the camera: a caller setting it would be repeating what
        // `SceneBuffers` already worked out, and could get it wrong without the shader noticing.
        const LightGrid& grid = inputs.mBuffers->getLightGrid();
        Shaders::VisibilityConstants pushed = constants;
        pushed.mGridOrigin = grid.getOrigin();
        pushed.mGridInverseCell = grid.getInverseCell();
        pushed.mGridSize = grid.getSize();

        vkCmdPushConstants(commands, mPipeline.getLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushed), &pushed);
        vkCmdDispatch(commands, groupsFor(constants.mWidth), groupsFor(constants.mHeight), 1);
    }

}
