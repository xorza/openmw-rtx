#include "guipass.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <volk.h>

#include <components/rtx/renderer/guirenderer.hpp>
#include <components/rtx/shaders/gui.h>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/display/tonepass.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>

namespace Rtx
{
    namespace
    {
        /// One texture, pushed per batch. Nothing else: a GUI vertex carries its own colour and
        /// its own position, and there is no transform to hand down.
        constexpr std::array<VkDescriptorSetLayoutBinding, 1> sBindings{
            VkDescriptorSetLayoutBinding{ Shaders::GUI_BIND_TEXTURE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
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

        /// `gui.frag`'s table, which says the texture's `AlphaForm`.
        constexpr std::array<std::uint32_t, Shaders::GUI_SPEC_COUNT> specializationFor(const bool premultiplied)
        {
            std::array<std::uint32_t, Shaders::GUI_SPEC_COUNT> words{};
            words[Shaders::GUI_SPEC_PREMULTIPLIED] = premultiplied ? 1u : 0u;
            return words;
        }

        constexpr std::array<std::uint32_t, Shaders::GUI_SPEC_COUNT> sStraight = specializationFor(false);
        constexpr std::array<std::uint32_t, Shaders::GUI_SPEC_COUNT> sPremultiplied = specializationFor(true);

        GraphicsPipelineOptions describePipeline(VkFormat targetFormat, Blend blend, AlphaForm source)
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
            options.mVertexModule = "gui.vert.spv";
            options.mFragmentModule = "gui.frag.spv";
            if (blend == Blend::Additive)
                options.mName = premultiplied ? "gui additive premultiplied" : "gui additive";
            else
                options.mName = premultiplied ? "gui premultiplied" : "gui";
            return options;
        }
    }

    GuiPass::GuiPass(const Device& device)
        : mOver(device, describePipeline(TonePass::sTargetFormat, Blend::Over, AlphaForm::Straight))
        , mAdditive(device, describePipeline(TonePass::sTargetFormat, Blend::Additive, AlphaForm::Straight))
        , mOverPremultiplied(device, describePipeline(TonePass::sTargetFormat, Blend::Over, AlphaForm::Premultiplied))
        , mAdditivePremultiplied(
              device, describePipeline(TonePass::sTargetFormat, Blend::Additive, AlphaForm::Premultiplied))
        , mSampler(makeTargetSampler(device, "gui"))
    {
    }

    const GraphicsPipeline<NoConstants>& GuiPass::pipelineFor(const GuiDraw& draw) const
    {
        if (draw.mSource == AlphaForm::Premultiplied)
            return draw.mBlend == Blend::Additive ? mAdditivePremultiplied : mOverPremultiplied;

        return draw.mBlend == Blend::Additive ? mAdditive : mOver;
    }

    void GuiPass::record(
        VkCommandBuffer commands, const Image& target, const Buffer& vertices, std::span<const GuiDraw> draws) const
    {
        if (draws.empty())
            return;

        // MyGUI computes its vertices for a clip space with +Y up, which is OpenGL's.
        beginDrawingOver(commands, target, ClipUp::Up);

        // Named by hand, because a vertex buffer is bound by handle and not handed out as an
        // address or a descriptor.
        const VkBuffer handle = vertices.getHandle();
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commands, 0, 1, &handle, &offset);
        vertices.nameForNext();

        const Pipeline* bound = nullptr;

        for (const GuiDraw& draw : draws)
        {
            const GraphicsPipeline<NoConstants>& pipeline = pipelineFor(draw);
            if (&pipeline != bound)
            {
                bind(commands, pipeline);
                bound = &pipeline;
            }

            // Against the layout of the pipeline that is bound: the four are identical, but a push
            // is only defined against the one in force.
            DescriptorWrites texture(pipeline);
            texture.image(Shaders::GUI_BIND_TEXTURE,
                VkDescriptorImageInfo{ mSampler.get(), draw.mTexture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
            pushDescriptors(commands, pipeline, texture);
            vkCmdDraw(commands, draw.mVertexCount, 1, draw.mFirstVertex, 0);
        }

        vkCmdEndRendering(commands);
    }
}
