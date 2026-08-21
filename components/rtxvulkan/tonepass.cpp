#include "tonepass.hpp"

#include <array>

#include "image.hpp"

namespace Rtx
{
    namespace
    {
        std::uint32_t groupsFor(std::uint32_t extent)
        {
            return (extent + Shaders::TONE_WORKGROUP - 1) / Shaders::TONE_WORKGROUP;
        }

        /// The frame in, the picture out. Both storage images, both pushed.
        constexpr std::array<VkDescriptorSetLayoutBinding, 2> sBindings{
            VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        };
    }

    TonePass::TonePass(const Device& device, const std::filesystem::path& shaderDirectory)
        : mPipeline(device, sBindings, sizeof(Shaders::ToneConstants), {}, shaderDirectory / "tone.comp.spv", "tone")
    {
    }

    void TonePass::record(VkCommandBuffer commands, const Image& colour, const Image& target) const
    {
        const std::array<VkDescriptorImageInfo, 2> images{
            VkDescriptorImageInfo{ VK_NULL_HANDLE, colour.getView(), VK_IMAGE_LAYOUT_GENERAL },
            VkDescriptorImageInfo{ VK_NULL_HANDLE, target.getView(), VK_IMAGE_LAYOUT_GENERAL },
        };

        std::array<VkWriteDescriptorSet, 2> writes{};
        for (std::uint32_t i = 0; i < images.size(); ++i)
            writes[i] = VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = i,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .pImageInfo = &images[i],
            };

        // **The target's size and not the colour's**, because with an upscaler between them the two
        // differ — and a dispatch sized to the smaller one encodes a corner of the frame and leaves
        // the rest of the image as it was found.
        const Shaders::ToneConstants constants{
            .mWidth = target.getWidth(),
            .mHeight = target.getHeight(),
        };

        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline.getHandle());
        vkCmdPushDescriptorSet(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline.getLayout(), 0,
            static_cast<std::uint32_t>(writes.size()), writes.data());
        vkCmdPushConstants(
            commands, mPipeline.getLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commands, groupsFor(constants.mWidth), groupsFor(constants.mHeight), 1);
    }
}
