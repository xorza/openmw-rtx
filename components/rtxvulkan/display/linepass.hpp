#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/graphicspipeline.hpp>
#include <components/rtxvulkan/shaders/shared/line.h>

namespace Rtx
{
    class Buffer;
    class Device;
    class Image;
    class ShaderCode;

    /// What one draw of the debug lines is over. A record and not an argument list, because two
    /// of the fields are an `Image` and two more a count, and either pair takes the other's value
    /// without a word.
    struct Lines
    {
        /// What to draw over, in `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`. Loaded rather than
        /// cleared: the frame is already in it.
        const Image& mTarget;

        /// The trace's surface channel, in `GENERAL`, at the extent `mConstants.mTraced` names.
        const Image& mSurface;

        Shaders::LineConstants mConstants;

        /// The lines' vertices first and the triangles' after them, in `Rtx::DebugVertex` layout:
        /// `mLineCount` and then `mTriangleCount` of them. Named for the submit the draw rides,
        /// because it is bound by handle and a host write over it asks that submit.
        const Buffer& mVertices;
        std::uint32_t mLineCount = 0;
        std::uint32_t mTriangleCount = 0;
    };

    /// The game's debug lines and triangles, over the finished picture and under the interface:
    /// `shaders/line.h` says what they are and why they are rasterized. Beside `GuiPass` in shape
    /// — the same dynamic rendering over the same target — and unlike it in every fragment
    /// asking the trace whether it stands in front of what was drawn there.
    class LinePass
    {
    public:
        /// Draws over the curve's picture, in `TonePass::sTargetFormat`.
        explicit LinePass(const Device& device);

        void record(VkCommandBuffer commands, const Lines& what) const;

    private:
        /// The pipelines made of one read of their two files, which `code` holds until they are.
        LinePass(const Device& device, ShaderCode&& code);

        /// Two, because a topology is baked into a pipeline: the navmesh is triangles and its
        /// edges, the pathgrid its lines, the collision shapes both.
        GraphicsPipeline<Shaders::LineConstants> mLines;
        GraphicsPipeline<Shaders::LineConstants> mTriangles;
    };
}
