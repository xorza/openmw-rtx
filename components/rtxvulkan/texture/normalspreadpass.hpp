#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/normalspread.h>

namespace Rtx
{
    class Buffer;
    class Device;
    class Image;

    /// A normal map's spread, measured on the device as it arrives: `normalspread.comp`. One dispatch a level of the
    /// map from its second, each boxing the level before's means into its own and writing the roughness what it made
    /// has lost.
    ///
    /// A cone that reads a normal map at a coarse level reads the mean of the normals under it, which is flatter than
    /// any of them, so a lobe as narrow as the finest level's shines where the surface shines no way at all. What the
    /// level lost is the roughness the trace widens the lobe by (`normalMapSlopes`, Toksvig's remedy); it is measured
    /// off the map's finest level because a file's levels are made unit by the tool that made them. It costs a byte a
    /// texel over the map's second level and down, and while it is made a chain of float means in the room its arrival
    /// works every map's in (`TextureArrival`).
    class NormalSpreadPass
    {
    public:
        explicit NormalSpreadPass(const Device& device);

        /// Records level `level` of `spread` from `map`: `spread`'s first is what the map's second level lost, and so
        /// on down. `map` is met as a texture the trace samples; `spread`, half the map's extent with a level for every
        /// level the map has below its first, is met where a dispatch writes it. The means are carried in `means` from
        /// the texel `meansAt` (`meansTexels`). The caller orders the means of the level before against this one, which
        /// `TextureArrival` does for a level of every spread of a group at once.
        void recordLevel(VkCommandBuffer commands, const Image& map, const Image& spread, std::uint32_t level,
            const Buffer& means, std::uint32_t meansAt) const;

        /// How many texels of means a spread shaped as `spread` is carried through: one for each
        /// texel of each of its levels.
        static std::uint32_t meansTexels(const Image& spread);

    private:
        ComputePipeline<Shaders::NormalSpreadConstants> mPipeline;

        /// Where a range of a storage buffer may start, which each dispatch's own range of the means
        /// is bound from.
        VkDeviceSize mAlignment;
    };
}
