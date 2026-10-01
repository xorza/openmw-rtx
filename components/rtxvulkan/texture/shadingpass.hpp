#pragma once

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/shadingmap.h>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

namespace Rtx
{
    class Device;
    class Image;

    /// The estimate of the light painted into a texture, made on the device as the texture
    /// arrives: `shadingsum.comp` over the card and `shadingmap.comp` after it, which say why it is
    /// here and not on the host. Two dispatches a texture, into the map the trace samples beside it,
    /// through the texture's own sums: the caller's, so the sums of every texture of an arrival
    /// are dispatched together and the maps after one barrier (`TextureArrival`).
    class ShadingPass
    {
    public:
        /// What one texture's sums take: every cell of the card, which the sum writes and the map
        /// reads.
        static constexpr VkDeviceSize sSumBytes
            = VkDeviceSize{ Shaders::SHADING_EXTENT } * Shaders::SHADING_EXTENT * sizeof(Shaders::ShadingSum);

        explicit ShadingPass(const Device& device);

        /// Records the sum of `source` into `sums`, `sSumBytes` of them. `source` is met as a
        /// texture the trace samples, which is how an upload leaves it. The size is the image's
        /// own, which for a texture held to a smaller side is a level further down the file.
        ///
        /// @param punchThrough whether `source` is a BC1 file, whose blocks store black where
        ///        nothing was painted.
        void recordSum(
            VkCommandBuffer commands, const Image& source, const VkDescriptorBufferInfo& sums, bool punchThrough) const;

        /// Records `map` from the sums `recordSum` wrote of `source`, which the caller has ordered
        /// before it. `map` is met where a dispatch writes it.
        void recordMap(VkCommandBuffer commands, const Image& source, const Image& map,
            const VkDescriptorBufferInfo& sums, bool punchThrough) const;

    private:
        ComputePipeline<Shaders::ShadingConstants> mSum;
        ComputePipeline<Shaders::ShadingConstants> mMap;
    };
}
