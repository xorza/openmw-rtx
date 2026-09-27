#include "guipass.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <components/rtx/renderer.hpp>

#include "dispatch.hpp"
#include "image.hpp"
#include "pipeline.hpp"

namespace Rtx
{
    namespace
    {
        /// One texture, pushed per batch. Nothing else: a GUI vertex carries its own colour and
        /// its own position, and there is no transform to hand down.
        constexpr std::array<VkDescriptorSetLayoutBinding, 1> sBindings{
            VkDescriptorSetLayoutBinding{
                0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
        };

        constexpr std::array<VkVertexInputBindingDescription, 1> sVertexBindings{
            VkVertexInputBindingDescription{ 0, sizeof(GuiVertex), VK_VERTEX_INPUT_RATE_VERTEX },
        };

        /// The colour is four bytes read as a normalised vector by the hardware, which is what
        /// makes MyGUI's own packing free to consume: `ColourABGR` puts red in the low byte, which
        /// is what `R8G8B8A8_UNORM` reads first.
        constexpr std::array<VkVertexInputAttributeDescription, 3> sVertexAttributes{
            VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(GuiVertex, mX) },
            VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GuiVertex, mColour) },
            VkVertexInputAttributeDescription{ 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GuiVertex, mU) },
        };

        /// `gui.frag`'s `PREMULTIPLIED`, which is the texture's `AlphaForm`.
        constexpr std::array<std::uint32_t, 1> sStraight{ 0 };
        constexpr std::array<std::uint32_t, 1> sPremultiplied{ 1 };

        GraphicsPipelineOptions describePipeline(
            const std::filesystem::path& shaderDirectory, VkFormat targetFormat, Blend blend, AlphaForm source)
        {
            const bool premultiplied = source == AlphaForm::Premultiplied;

            GraphicsPipelineOptions options;
            options.mBindings = sBindings;
            options.mVertexBindings = sVertexBindings;
            options.mVertexAttributes = sVertexAttributes;
            options.mColourFormat = targetFormat;
            options.mBlend = blend;
            options.mSource = source;
            options.mSpecialization = premultiplied ? sPremultiplied : sStraight;
            options.mVertexModule = shaderDirectory / "gui.vert.spv";
            options.mFragmentModule = shaderDirectory / "gui.frag.spv";
            if (blend == Blend::Additive)
                options.mName = premultiplied ? "gui additive premultiplied" : "gui additive";
            else
                options.mName = premultiplied ? "gui premultiplied" : "gui";
            return options;
        }
    }

    GuiPass::GuiPass(const Device& device, const std::filesystem::path& shaderDirectory, VkFormat targetFormat)
        : mOver(device, describePipeline(shaderDirectory, targetFormat, Blend::Over, AlphaForm::Straight))
        , mAdditive(device, describePipeline(shaderDirectory, targetFormat, Blend::Additive, AlphaForm::Straight))
        , mOverPremultiplied(
              device, describePipeline(shaderDirectory, targetFormat, Blend::Over, AlphaForm::Premultiplied))
        , mAdditivePremultiplied(
              device, describePipeline(shaderDirectory, targetFormat, Blend::Additive, AlphaForm::Premultiplied))
        , mSampler(makeTargetSampler(device, "gui"))
    {
    }

    const GraphicsPipeline& GuiPass::pipelineFor(const GuiDraw& draw) const
    {
        if (draw.mSource == AlphaForm::Premultiplied)
            return draw.mBlend == Blend::Additive ? mAdditivePremultiplied : mOverPremultiplied;

        return draw.mBlend == Blend::Additive ? mAdditive : mOver;
    }

    void GuiPass::record(
        VkCommandBuffer commands, const Image& target, VkBuffer vertices, std::span<const GuiDraw> draws) const
    {
        if (draws.empty())
            return;

        // MyGUI computes its vertices for a clip space with +Y up, which is OpenGL's.
        beginDrawingOver(commands, target, ClipUp::Up);

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commands, 0, 1, &vertices, &offset);

        const GraphicsPipeline* bound = nullptr;

        for (const GuiDraw& draw : draws)
        {
            const GraphicsPipeline& pipeline = pipelineFor(draw);
            if (&pipeline != bound)
            {
                bind(commands, pipeline);
                bound = &pipeline;
            }

            // Against the layout of the pipeline that is bound: the four are identical, but a push
            // is only defined against the one in force.
            DescriptorWrites<1> texture;
            texture.image(0,
                VkDescriptorImageInfo{ mSampler.get(), draw.mTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            pushDescriptors(commands, pipeline, texture.get());
            vkCmdDraw(commands, draw.mVertexCount, 1, draw.mFirstVertex, 0);
        }

        vkCmdEndRendering(commands);
    }
}
