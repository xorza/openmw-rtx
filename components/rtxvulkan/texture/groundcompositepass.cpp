#include "groundcompositepass.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <span>

#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/shaders/shared/ground.h>

namespace Rtx
{
    namespace
    {
        /// The two finest levels, out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::GROUND_COMPOSITE_BINDINGS> sBindings{
            computeBinding(Shaders::GROUND_COMPOSITE_BIND_ALBEDO, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::GROUND_COMPOSITE_BIND_GLOSS, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        };
    }

    GroundCompositePass::GroundCompositePass(const Device& device, const SetLayout& textures)
        : mPipeline(device, sBindings, SharedSetLayouts{ .mTextures = &textures }, "groundcomposite.comp.spv",
            "ground composite")
        , mEncode(device)
        , mNoTarget(makeStandIn(
              device, toVulkanFormat(TEXTURE_WRITTEN_FORMAT), VK_IMAGE_USAGE_STORAGE_BIT, "no ground target"))
    {
    }

    void GroundCompositePass::record(const VkCommandBuffer commands, const VkDescriptorSet textures,
        const Image* const albedo, const Image* const gloss, const Shaders::GroundCompositeConstants& chunk) const
    {
        assert((albedo != nullptr) == ((chunk.mOutputs & Shaders::GROUND_COMPOSITE_ALBEDO) != 0u)
            && (gloss != nullptr) == ((chunk.mOutputs & Shaders::GROUND_COMPOSITE_GLOSS) != 0u)
            && "a bake told to write an image it was not handed, or handed one it was not told to write");

        std::array<const Image*, 2> written{};
        std::size_t count = 0;
        for (const Image* image : { albedo, gloss })
        {
            if (image == nullptr)
                continue;

            assert(image->getWidth() == Shaders::GROUND_COMPOSITE_EXTENT
                && image->getHeight() == Shaders::GROUND_COMPOSITE_EXTENT && "a composite of another size");
            image->transition(commands, Use::sUndefined, Use::sComputeWrite);
            written[count++] = image;
        }

        // The stand-in is written by no bake, but a layer that reads the shader's bindings and not its
        // branches counts every bake that binds it as a write. Two in a row are then a hazard it
        // reports, so the pair is ordered: a barrier on one texel, on a placement's few bakes.
        if (albedo == nullptr || gloss == nullptr)
            mNoTarget.transition(commands, Use::sComputeWrite, Use::sComputeWrite);

        DescriptorWrites writes(mPipeline);
        writes.image(
            Shaders::GROUND_COMPOSITE_BIND_ALBEDO, (albedo != nullptr ? *albedo : mNoTarget).describeStorage());
        writes.image(Shaders::GROUND_COMPOSITE_BIND_GLOSS, (gloss != nullptr ? *gloss : mNoTarget).describeStorage());

        // The scene's textures beside the pushed set, which the two are independent of: a pushed set
        // and a bound one only have to be in place by the dispatch.
        bindSets(commands, mPipeline, SharedSetBinds{ .mTextures = textures });

        dispatch(commands, mPipeline, writes, chunk,
            Groups::covering(Shaders::GROUND_COMPOSITE_EXTENT, Shaders::GROUND_COMPOSITE_EXTENT,
                Shaders::GROUND_COMPOSITE_WORKGROUP));

        // The chains, box filtered in light through each image's own format, which is the filter
        // the sum was made for.
        Image::buildMips(commands, std::span<const Image* const>(written.data(), count));
    }

    Misc::Result<Image, std::string_view> GroundCompositePass::makeCanvas(
        const Device& device, const bool gloss, std::string_view name)
    {
        constexpr std::uint32_t extent = Shaders::GROUND_COMPOSITE_EXTENT;
        constexpr VkFormat stored = toVulkanFormat(TEXTURE_WRITTEN_FORMAT);
        return Image::tryMake(MemoryUse::Texture, device, extent, extent, gloss ? stored : withCurve(stored),
            sWrittenTextureUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, name,
            levelsTo1x1(extent, extent), 1, stored);
    }

    Misc::Result<Buffer, std::string_view> GroundCompositePass::makeBlocks(const Device& device, std::string_view name)
    {
        constexpr std::uint32_t extent = Shaders::GROUND_COMPOSITE_EXTENT;
        return Buffer::tryMake(MemoryUse::Texture, device, BufferKind::DeviceLocal,
            Bc7Chain::of(extent, extent, levelsTo1x1(extent, extent)).mBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, name);
    }

    void GroundCompositePass::encode(
        const VkCommandBuffer commands, const Image& canvas, const Buffer& blocks, const Image& target) const
    {
        canvas.transition(commands, Use::sShaderSample, Use::sComputeRead);
        handOver(commands, Use::sBufferCopyRead, Use::sBufferComputeWrite);
        // Opaque: the bake stores an alpha of one in both images, and the ground is read as a solid.
        mEncode.record(commands, canvas, blocks, 0, target, false);
    }
}
