#pragma once

#include <string_view>

#include <vulkan/vulkan_core.h>

#include <components/misc/result.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>

#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/ground.h>

#include "bc7encodepass.hpp"

namespace Rtx
{
    class Buffer;
    class Device;

    /// A chunk's layer stack flattened into one texture on the device: `groundcomposite.comp`,
    /// which says why it is here and not on threads of the host. One dispatch a chunk and the
    /// chain blitted after it, in the placement that wrote the chunk's material row, because the
    /// sum reads the scene's tables and textures as that placement bound them. Then encoded into
    /// the chunk's BC7 slot (`encode`): a composite loose was nine tenths of a vanilla scene's
    /// textures.
    class GroundCompositePass
    {
    public:
        /// @param textures the layout of the scene's texture set, bound at `SET_TEXTURES` — the
        ///        layers' textures and their shading maps.
        GroundCompositePass(const Device& device, const SetLayout& textures);

        /// An image `record` bakes into: `GROUND_COMPOSITE_EXTENT` square with a chain to one texel, a
        /// storage view with no curve and both transfer usages for the chain's blits. The albedo's
        /// under the curve, so its chain is filtered in light; the gloss's as data. Or why the
        /// device has no room for it.
        static Misc::Result<Image, std::string_view> makeCanvas(
            const Device& device, bool gloss, std::string_view name);

        /// The blocks `encode` writes a canvas's chain through, or why the device has no room.
        static Misc::Result<Buffer, std::string_view> makeBlocks(const Device& device, std::string_view name);

        /// Records `chunk`'s bake into its albedo and its gloss, every level of each: one sum into
        /// both first levels, the chains blitted below them in step. Each is met undefined and left
        /// as the chain's blits leave it (`Image::buildMips`), for `encode`, and is what
        /// `makeCanvas` makes.
        ///
        /// @param textures the scene's texture set for the copy `chunk`'s tables are of.
        /// @param albedo,gloss null where `chunk.mOutputs` does not name it, and only there.
        void record(VkCommandBuffer commands, VkDescriptorSet textures, const Image* albedo, const Image* gloss,
            const Shaders::GroundCompositeConstants& chunk) const;

        /// Encodes `canvas`, one of `record`'s images as it left it, into `target`, the chunk's BC7
        /// slot, through `blocks` (`Bc7EncodePass::record`). `blocks` may still be read by the copy
        /// out of the last `encode`.
        void encode(VkCommandBuffer commands, const Image& canvas, const Buffer& blocks, const Image& target) const;

    private:
        ComputePipeline<Shaders::GroundCompositeConstants> mPipeline;
        Bc7EncodePass mEncode;

        /// What an image the bake does not write is bound as, because a descriptor has to point
        /// somewhere: a chunk whose two images arrived apart is baked once for each.
        Image mNoTarget;
    };
}
