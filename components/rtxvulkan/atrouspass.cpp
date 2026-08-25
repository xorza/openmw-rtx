#include "atrouspass.hpp"

#include <array>
#include <cassert>

#include <components/rtx/shaders/gbuffer.h>

#include "gbuffer.hpp"

namespace Rtx
{
    namespace
    {
        std::uint32_t groupsFor(std::uint32_t extent)
        {
            return (extent + Shaders::ATROUS_WORKGROUP - 1) / Shaders::ATROUS_WORKGROUP;
        }

        /// The channel coming in, the channel going out, and the two that say where the edges are:
        /// normals from the guide and distances from the depth. All storage images, all pushed.
        constexpr std::array<VkDescriptorSetLayoutBinding, 4> sBindings{
            VkDescriptorSetLayoutBinding{ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
            VkDescriptorSetLayoutBinding{ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT },
        };

        /// How sharply a tap's normal has to agree with the centre's, and how far off its plane it
        /// may sit.
        ///
        /// The exponent is SVGF's own; the sigma is not, because the test it scales is not SVGF's
        /// either — that paper divides by a depth gradient and this measures a distance off a
        /// plane, so two is a figure in pixel footprints rather than in its units.
        ///
        /// Both are here rather than in a setting because nothing yet knows what to set them to:
        /// the reference mode is what will say, and a dial offered before then is a dial nobody can
        /// turn on evidence.
        constexpr float sNormalPower = 128.0f;
        constexpr float sPlaneSigma = 2.0f;
    }

    AtrousPass::AtrousPass(const Device& device, const std::filesystem::path& shaderDirectory)
        : mDevice(device)
        , mPipeline(
              device, sBindings, sizeof(Shaders::AtrousConstants), {}, shaderDirectory / "atrous.comp.spv", "atrous")
    {
    }

    void AtrousPass::resize(std::uint32_t width, std::uint32_t height)
    {
        if (mScratch != nullptr && mScratch->getWidth() == width && mScratch->getHeight() == height)
            return;

        mScratch = std::make_unique<Image>(
            mDevice, width, height, GBUFFER_RADIANCE, VK_IMAGE_USAGE_STORAGE_BIT, "atrous-scratch");
    }

    const Image& AtrousPass::record(
        VkCommandBuffer commands, const GBuffer& buffer, const Shaders::VisibilityConstants& camera) const
    {
        assert(mScratch != nullptr && "record before resize");
        assert(mScratch->getWidth() >= camera.mWidth && mScratch->getHeight() >= camera.mHeight);
        assert(buffer.getWidth() >= camera.mWidth && buffer.getHeight() >= camera.mHeight);

        // Nothing has written the scratch yet this frame, so the first level may discard it. Every
        // level after reads what the one before wrote, which is what the barriers below order.
        mScratch->transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

        Shaders::AtrousConstants level{
            .mForward = camera.mForward,
            .mRight = camera.mRight,
            .mUp = camera.mUp,
            .mOrthographic = camera.mOrthographic,
            .mWidth = camera.mWidth,
            .mHeight = camera.mHeight,
            .mJitter = camera.mJitter,
            .mSpreadAngle = camera.mSpreadAngle,
            .mStep = 1,
            .mNormalPower = sNormalPower,
            .mPlaneSigma = sPlaneSigma,
        };

        const Image* source = &buffer.getIndirect();
        const Image* target = mScratch.get();

        for (std::uint32_t pass = 0; pass < Shaders::ATROUS_LEVELS; ++pass)
        {
            if (pass > 0)
            {
                // The level about to run reads what the last one wrote and overwrites what it read,
                // so both channels have to be ordered against it — the second is a write after a
                // read, which needs the stages named and nothing made visible.
                for (const Image* image : { source, target })
                    image->transition(commands, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
            }

            const std::array<VkDescriptorImageInfo, 4> images{
                VkDescriptorImageInfo{ VK_NULL_HANDLE, source->getView(), VK_IMAGE_LAYOUT_GENERAL },
                VkDescriptorImageInfo{ VK_NULL_HANDLE, target->getView(), VK_IMAGE_LAYOUT_GENERAL },
                VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getGuide().getView(), VK_IMAGE_LAYOUT_GENERAL },
                VkDescriptorImageInfo{ VK_NULL_HANDLE, buffer.getDepth().getView(), VK_IMAGE_LAYOUT_GENERAL },
            };

            std::array<VkWriteDescriptorSet, 4> writes{};
            for (std::uint32_t i = 0; i < images.size(); ++i)
                writes[i] = VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstBinding = i,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .pImageInfo = &images[i],
                };

            level.mStep = 1u << pass;

            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline.getHandle());
            vkCmdPushDescriptorSet(commands, VK_PIPELINE_BIND_POINT_COMPUTE, mPipeline.getLayout(), 0,
                static_cast<std::uint32_t>(writes.size()), writes.data());
            vkCmdPushConstants(commands, mPipeline.getLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(level), &level);
            vkCmdDispatch(commands, groupsFor(camera.mWidth), groupsFor(camera.mHeight), 1);

            std::swap(source, target);
        }

        // One swap past the last dispatch, so this is what that dispatch wrote.
        return *source;
    }
}
