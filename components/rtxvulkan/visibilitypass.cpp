#include "visibilitypass.hpp"

#include <array>
#include <cassert>

#include <components/rtx/bluenoise.hpp>

#include "buffer.hpp"
#include "commands.hpp"
#include "device.hpp"
#include "gbuffer.hpp"
#include "result.hpp"
#include "scenebuffers.hpp"
#include "shadermodule.hpp"

namespace Rtx
{
    namespace
    {
        std::uint32_t groupsFor(std::uint32_t extent)
        {
            return (extent + Shaders::VISIBILITY_WORKGROUP - 1) / Shaders::VISIBILITY_WORKGROUP;
        }
    }

    VisibilityPass::VisibilityPass(const Device& device, CommandPool& pool,
        const std::filesystem::path& shaderDirectory, VkDescriptorSetLayout textureLayout)
        : mDevice(device)
        , mBlueNoise(uploadBuffer(device, pool, BlueNoise::shared().getValues(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
        , mTextureLayout(textureLayout)
    {
        constexpr auto compute = VK_SHADER_STAGE_COMPUTE_BIT;
        constexpr auto storage = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        constexpr std::array<VkDescriptorSetLayoutBinding, 19> bindings{
            VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 2, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 3, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 4, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 5, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 6, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 7, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 8, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 9, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 10, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 11, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 12, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 13, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 14, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 15, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 16, storage, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 17, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr },
            VkDescriptorSetLayoutBinding{ 18, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, compute, nullptr },
        };

        const VkDescriptorSetLayoutCreateInfo layout{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
            .bindingCount = static_cast<std::uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        checkVk(vkCreateDescriptorSetLayout(device.getHandle(), &layout, nullptr, &mSetLayout),
            "vkCreateDescriptorSetLayout");

        const VkPushConstantRange range{
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .size = sizeof(Shaders::VisibilityConstants),
        };
        const std::array<VkDescriptorSetLayout, 2> sets{ mSetLayout, mTextureLayout };
        const VkPipelineLayoutCreateInfo pipelineLayout{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<std::uint32_t>(sets.size()),
            .pSetLayouts = sets.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &range,
        };
        checkVk(vkCreatePipelineLayout(device.getHandle(), &pipelineLayout, nullptr, &mPipelineLayout),
            "vkCreatePipelineLayout");

        const ShaderModule module(device, shaderDirectory / "visibility.comp.spv");
        const VkComputePipelineCreateInfo pipeline{
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = module.getHandle(),
                .pName = "main",
            },
            .layout = mPipelineLayout,
        };
        checkVk(
            vkCreateComputePipelines(device.getHandle(), device.getPipelineCache(), 1, &pipeline, nullptr, &mPipeline),
            "vkCreateComputePipelines");

        device.setName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(mPipeline), "visibility");
    }

    VisibilityPass::~VisibilityPass()
    {
        if (mPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(mDevice.getHandle(), mPipeline, nullptr);
        if (mPipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(mDevice.getHandle(), mPipelineLayout, nullptr);
        if (mSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(mDevice.getHandle(), mSetLayout, nullptr);
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
        // One, then fifteen, seventeen, eighteen: the channels grew onto the end of a layout that
        // already existed rather than renumbering every table under them.
        const std::array<VkDescriptorImageInfo, 4> channels{
            VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getDirect().getView(), VK_IMAGE_LAYOUT_GENERAL },
            VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getIndirect().getView(), VK_IMAGE_LAYOUT_GENERAL },
            VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getModulate().getView(), VK_IMAGE_LAYOUT_GENERAL },
            VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getGuide().getView(), VK_IMAGE_LAYOUT_GENERAL },
        };
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

        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline);
        vkCmdPushDescriptorSet(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineLayout, 0,
            static_cast<std::uint32_t>(writes.size()), writes.data());
        vkCmdBindDescriptorSets(
            commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineLayout, 1, 1, &inputs.mTextures, 0, nullptr);
        // The grid's geometry belongs to the lamps it was binned from, so it is filled here rather
        // than by whoever assembled the camera: a caller setting it would be repeating what
        // `SceneBuffers` already worked out, and could get it wrong without the shader noticing.
        const LightGrid& grid = inputs.mBuffers->getLightGrid();
        Shaders::VisibilityConstants pushed = constants;
        pushed.mGridOrigin = grid.getOrigin();
        pushed.mGridInverseCell = grid.getInverseCell();
        pushed.mGridSize = grid.getSize();

        vkCmdPushConstants(commands, mPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushed), &pushed);
        vkCmdDispatch(commands, groupsFor(constants.mWidth), groupsFor(constants.mHeight), 1);
    }

}
