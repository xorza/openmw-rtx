#include "linepass.hpp"

#include <array>
#include <cassert>
#include <cstddef>

#include <components/rtx/frame/debuglines.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>

#include "tonepass.hpp"

namespace Rtx
{
    namespace
    {
        /// The trace's surface channel, whose distance every fragment reads.
        constexpr std::array<VkDescriptorSetLayoutBinding, 1> sBindings{
            VkDescriptorSetLayoutBinding{
                Shaders::LINE_BIND_SURFACE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT },
        };

        constexpr std::array<VkVertexInputBindingDescription, 1> sVertexBindings{
            VkVertexInputBindingDescription{ 0, sizeof(DebugVertex), VK_VERTEX_INPUT_RATE_VERTEX },
        };

        constexpr std::array<VkVertexInputAttributeDescription, 2> sVertexAttributes{
            VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(DebugVertex, mPosition) },
            VkVertexInputAttributeDescription{ 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(DebugVertex, mColour) },
        };

        GraphicsPipelineOptions describePipeline(VkFormat targetFormat, VkPrimitiveTopology topology)
        {
            GraphicsPipelineOptions options;
            options.mBindings = sBindings;
            options.mVertexBindings = sVertexBindings;
            options.mVertexAttributes = sVertexAttributes;
            options.mColourFormat = targetFormat;
            options.mBlend = Blend::Over;
            options.mTopology = topology;
            options.mVertexModule = "line.vert.spv";
            options.mFragmentModule = "line.frag.spv";
            options.mName = topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST ? "debug lines" : "debug triangles";
            return options;
        }
    }

    LinePass::LinePass(const Device& device)
        : mLines(device, describePipeline(TonePass::sTargetFormat, VK_PRIMITIVE_TOPOLOGY_LINE_LIST))
        , mTriangles(device, describePipeline(TonePass::sTargetFormat, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST))
    {
    }

    void LinePass::record(const VkCommandBuffer commands, const Lines& what) const
    {
        const Image& target = what.mTarget;
        const Image& surface = what.mSurface;
        const Shaders::LineConstants& constants = what.mConstants;
        const Buffer& vertices = what.mVertices;
        const std::uint32_t lineCount = what.mLineCount;
        const std::uint32_t triangleCount = what.mTriangleCount;

        assert(constants.mCamera.mOrthographic == 0 && "debug lines through a parallel projection");
        assert(constants.mCamera.mWidth == target.getWidth() && constants.mCamera.mHeight == target.getHeight()
            && "a camera on a grid other than the target's");
        assert(constants.mTraced.x() == surface.getWidth() && constants.mTraced.y() == surface.getHeight()
            && "a traced extent other than the surface channel's");

        if (lineCount == 0 && triangleCount == 0)
            return;

        // `line.vert` writes Vulkan's own clip space, `+Y` down as the picture is indexed.
        beginDrawingOver(commands, target, ClipUp::Down);

        const VkBuffer bound = vertices.getHandle();
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commands, 0, 1, &bound, &offset);
        vertices.nameForNext();

        // Each pipeline is handed the set and the block again: a push is only defined against
        // the layout in force, and the two layouts are the same in everything but the handle.
        const auto draw
            = [&](const GraphicsPipeline<Shaders::LineConstants>& pipeline, std::uint32_t first, std::uint32_t count) {
                  if (count == 0)
                      return;

                  DescriptorWrites traced(pipeline);
                  traced.image(Shaders::LINE_BIND_SURFACE, surface.describeStorage());

                  bind(commands, pipeline);
                  pushDescriptors(commands, pipeline, traced);
                  pipeline.push(commands, constants);
                  vkCmdDraw(commands, count, 1, first, 0);
              };

        draw(mLines, 0, lineCount);
        draw(mTriangles, lineCount, triangleCount);

        vkCmdEndRendering(commands);
    }
}
