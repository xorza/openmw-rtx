#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include <volk.h>

#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/owned.hpp>

#include "pipeline.hpp"

namespace Rtx
{
    class Device;
    class Image;

    /// How what a pipeline draws reaches what is already in the attachment.
    enum class Blend
    {
        /// Straight through: what is written replaces what is there.
        None,

        /// Source alpha over what is there, and the source's own alpha accumulated the way a
        /// premultiplied composite wants it.
        Over,

        /// Added to what is there, scaled by its own alpha.
        Additive,
    };

    /// How a source's colour stands to its alpha, which is what the blend's source factor is.
    enum class AlphaForm
    {
        /// Apart: the blend weighs the colour by the alpha. Every texture the host writes is this —
        /// a font, a skin, a video frame.
        Straight,

        /// Already weighed by it, so the blend takes the colour as it is. A traced picture is this,
        /// because it is the one form that holds light over nothing: a flame in a doll's hand,
        /// against the window behind the doll, has a colour and no coverage at all.
        Premultiplied,
    };

    /// What a raster pipeline is made of that a compute one has no equivalent for. No span
    /// outlives the call: every one is read into Vulkan's own copies inside the constructor, so a
    /// caller may pass the address of one of its own locals.
    struct GraphicsPipelineOptions
    {
        /// Set zero, which is always a push descriptor set: nothing in this renderer wants a
        /// descriptor pool on the frame path.
        std::span<const VkDescriptorSetLayoutBinding> mBindings;

        std::span<const VkVertexInputBindingDescription> mVertexBindings;
        std::span<const VkVertexInputAttributeDescription> mVertexAttributes;

        /// The format of the one colour attachment. Dynamic rendering, so there is no render pass
        /// and no framebuffer, and one object serves every target size.
        VkFormat mColourFormat = VK_FORMAT_UNDEFINED;

        Blend mBlend = Blend::None;

        /// What the fragment stage writes, `AlphaForm`. Read only where something blends.
        AlphaForm mSource = AlphaForm::Straight;

        /// One word per specialization constant, as `ComputePipeline` takes them, given to both
        /// stages.
        std::span<const std::uint32_t> mSpecialization;

        /// What the vertices make: triangles, which the interface is, or lines, which the debug
        /// modes are.
        VkPrimitiveTopology mTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        /// The stages' compiled SPIR-V, by name in the device's shader directory.
        std::string_view mVertexModule;
        std::string_view mFragmentModule;

        /// What a capture calls the pipeline.
        std::string_view mName;
    };

    /// A graphics pipeline's handle against `layout`: the part of `GraphicsPipeline` its constants
    /// do not decide.
    Owned<VkPipeline, vkDestroyPipeline> makeGraphicsPipeline(
        const Device& device, VkPipelineLayout layout, const GraphicsPipelineOptions& options);

    /// A graphics pipeline and its layout, pushed a `Constants` to both stages. The one thing in
    /// this backend that is not compute, because there is nothing to be gained by tracing a font
    /// atlas.
    template <class Constants>
    class GraphicsPipeline : public TypedPipeline<Constants>
    {
    public:
        GraphicsPipeline(const Device& device, const GraphicsPipelineOptions& options)
            : GraphicsPipeline(device,
                PipelineLayout(device, options.mBindings,
                    pushRangeOf<Constants>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT), {}),
                options)
        {
        }

    private:
        GraphicsPipeline(const Device& device, PipelineLayout&& layout, const GraphicsPipelineOptions& options)
            : TypedPipeline<Constants>(std::move(layout), makeGraphicsPipeline(device, layout.getHandle(), options),
                VK_PIPELINE_BIND_POINT_GRAPHICS)
        {
        }
    };

    /// Which way the clip space a pass writes has `+Y`: Vulkan's own, down as a picture is
    /// indexed, or OpenGL's, up, which MyGUI computes its vertices for and a flipped viewport
    /// answers at no cost.
    enum class ClipUp
    {
        Down,
        Up,
    };

    /// What a draw over a target does with what the target holds.
    enum class Underneath
    {
        /// Loaded, because the picture is already in it.
        Kept,

        /// Left undefined, because the first draw writes every pixel.
        Replaced,
    };

    /// Begins drawing over the whole of `target`, with the viewport and the scissor set to its
    /// extent. Dynamic rendering, so there is no render pass and no framebuffer;
    /// `vkCmdEndRendering` ends it.
    ///
    /// @param target in `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` and made with
    ///        `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`, which is asserted.
    void beginDrawingOver(VkCommandBuffer commands, const Image& target, ClipUp up, Underneath underneath);
}
