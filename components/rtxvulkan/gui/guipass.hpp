#pragma once

#include <cstdint>
#include <span>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/pipeline/graphicspipeline.hpp>

namespace Rtx
{
    class Device;
    class Image;

    /// One run of vertices drawn with one texture. A run and not an index range, because MyGUI
    /// hands over triangle lists and no indices: a batch is a stretch of the vertex buffer and a
    /// texture to read while drawing it.
    struct GuiDraw
    {
        VkImageView mTexture = VK_NULL_HANDLE;
        std::uint32_t mFirstVertex = 0;
        std::uint32_t mVertexCount = 0;

        /// Which of the pass's pipelines draws it: how it reaches what is there, and what its
        /// texture holds. Runs are recorded in the order given and the pipeline is bound again only
        /// where it changes, so a caller that keeps like with like pays for one bind.
        Blend mBlend = Blend::Over;
        AlphaForm mSource = AlphaForm::Straight;
    };

    /// The GUI, over the finished picture — after tone mapping, because MyGUI picked its colours
    /// looking at a monitor and a curve meant for radiance turns a menu grey. The only triangles
    /// in this backend, because a font atlas is not something to trace.
    class GuiPass
    {
    public:
        /// Draws over the curve's picture, in `TonePass::sTargetFormat`.
        explicit GuiPass(const Device& device);

        /// @param target what to draw over, in `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` and made
        ///        with `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`, which is asserted. Loaded rather than
        ///        cleared: the frame is already in it.
        /// @param vertices every batch's vertices in one buffer, in `Rtx::GuiVertex` layout.
        /// @param draws what to draw and what to read while drawing it, in order. Each texture must
        ///        be in `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`.
        void record(
            VkCommandBuffer commands, const Image& target, VkBuffer vertices, std::span<const GuiDraw> draws) const;

    private:
        const GraphicsPipeline<NoConstants>& pipelineFor(const GuiDraw& draw) const;

        /// Four, because a blend mode is baked into a pipeline: over or added, of a straight texture
        /// or a premultiplied one. The alternative is `VK_EXT_extended_dynamic_state3`, which is a
        /// device feature to require and a driver path to trust for something that is four objects
        /// compiled once at startup.
        GraphicsPipeline<NoConstants> mOver;
        GraphicsPipeline<NoConstants> mAdditive;
        GraphicsPipeline<NoConstants> mOverPremultiplied;
        GraphicsPipeline<NoConstants> mAdditivePremultiplied;

        /// Clamped, because a widget's atlas entry runs to the edge of what it was given and
        /// wrapping would fetch the glyph next to it.
        Sampler mSampler;
    };
}
