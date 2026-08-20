#include "visibilitypass.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>

#include <osg/Math>

#include "buffer.hpp"
#include "device.hpp"
#include "error.hpp"
#include "image.hpp"
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

    Shaders::VisibilityConstants makeCamera(const osg::Vec3f& origin, const osg::Vec3f& target,
        float verticalFovDegrees, std::uint32_t width, std::uint32_t height, float far)
    {
        assert(width > 0 && height > 0);

        osg::Vec3f forward = target - origin;
        if (forward.length2() <= 0.0f)
            throw Error("the camera is standing where it is looking");
        forward.normalize();

        const osg::Vec3f worldUp(0.0f, 0.0f, 1.0f);
        osg::Vec3f right = forward ^ worldUp;
        if (right.length2() <= 1e-6f)
            throw Error("the camera looks along the world's up axis, which leaves its roll undefined");
        right.normalize();

        osg::Vec3f up = right ^ forward;
        up.normalize();

        const float halfHeight = std::tan(osg::DegreesToRadians(verticalFovDegrees) * 0.5f);
        const float halfWidth = halfHeight * static_cast<float>(width) / static_cast<float>(height);

        // The vertical angle one pixel covers. Pixels are square here, so one number does for both.
        const float spread = std::atan(2.0f * halfHeight / static_cast<float>(height));

        return Shaders::VisibilityConstants{
            .mOrigin = origin,
            .mForward = forward,
            .mRight = right * halfWidth,
            .mUp = up * halfHeight,
            .mWidth = width,
            .mHeight = height,
            .mFar = far,
            .mSpreadAngle = spread,
            // Not zero, which would be sea level: a world with no water has to answer "how deep is
            // this point" with never, and only an infinity does that without a second question.
            .mWaterLevel = -std::numeric_limits<float>::infinity(),
        };
    }

    VisibilityPass::VisibilityPass(
        const Device& device, const std::filesystem::path& shaderDirectory, VkDescriptorSetLayout textureLayout)
        : mDevice(device)
        , mTextureLayout(textureLayout)
    {
        constexpr auto compute = VK_SHADER_STAGE_COMPUTE_BIT;
        constexpr auto storage = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        constexpr std::array<VkDescriptorSetLayoutBinding, 15> bindings{
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
        checkVk(vkCreateComputePipelines(device.getHandle(), VK_NULL_HANDLE, 1, &pipeline, nullptr, &mPipeline),
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

    void VisibilityPass::record(VkCommandBuffer commands, const VisibilityInputs& inputs, const Image& target,
        const Buffer& hitCount, const Shaders::VisibilityConstants& constants) const
    {
        const VkWriteDescriptorSetAccelerationStructureKHR sceneWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
            .accelerationStructureCount = 1,
            .pAccelerationStructures = &inputs.mScene,
        };
        const VkDescriptorImageInfo targetWrite{
            .imageView = target.getView(),
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };

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

        std::array<VkWriteDescriptorSet, 15> writes{};
        writes[0] = VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = &sceneWrite,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        };
        writes[1] = VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &targetWrite,
        };
        for (std::uint32_t i = 0; i < buffers.size(); ++i)
            writes[i + 2] = VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = i + 2,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &buffers[i],
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
