#include "visibilitypass.hpp"

#include <array>
#include <cassert>
#include <cmath>

#include <osg/Math>

#include "buffer.hpp"
#include "device.hpp"
#include "error.hpp"
#include "image.hpp"
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

        return Shaders::VisibilityConstants{
            .mOrigin = origin,
            .mForward = forward,
            .mRight = right * halfWidth,
            .mUp = up * halfHeight,
            .mWidth = width,
            .mHeight = height,
            .mFar = far,
        };
    }

    VisibilityPass::VisibilityPass(const Device& device, const std::filesystem::path& shaderDirectory)
        : mDevice(device)
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, 3> bindings{
            VkDescriptorSetLayoutBinding{
                0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{
                1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            VkDescriptorSetLayoutBinding{
                2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
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
        const VkPipelineLayoutCreateInfo pipelineLayout{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &mSetLayout,
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

    void VisibilityPass::record(VkCommandBuffer commands, VkAccelerationStructureKHR scene, const Image& target,
        const Buffer& hitCount, const Shaders::VisibilityConstants& constants) const
    {
        const VkWriteDescriptorSetAccelerationStructureKHR sceneWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
            .accelerationStructureCount = 1,
            .pAccelerationStructures = &scene,
        };
        const VkDescriptorImageInfo targetWrite{
            .imageView = target.getView(),
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        };
        const VkDescriptorBufferInfo hitWrite{
            .buffer = hitCount.getHandle(),
            .range = VK_WHOLE_SIZE,
        };

        const std::array<VkWriteDescriptorSet, 3> writes{
            VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext = &sceneWrite,
                .dstBinding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            },
            VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = 1,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &targetWrite,
            },
            VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = 2,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &hitWrite,
            },
        };

        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline);
        vkCmdPushDescriptorSet(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipelineLayout, 0,
            static_cast<std::uint32_t>(writes.size()), writes.data());
        vkCmdPushConstants(commands, mPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commands, groupsFor(constants.mWidth), groupsFor(constants.mHeight), 1);
    }
}
