#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include "buffer.hpp"
#include "computepipeline.hpp"

namespace Rtx
{
    class Device;
    class Image;

    /// The estimate of the light painted into a texture, made on the device as the texture
    /// arrives: `shadingsum.comp` over the card and `shadingmap.comp` after it, which say why it is
    /// here and not on the host. Two dispatches a texture, into the map the trace samples beside it.
    class ShadingPass
    {
    public:
        ShadingPass(const Device& device, const std::filesystem::path& shaderDirectory);

        /// Records `source`'s estimate into `map`. `source` is met as a texture the trace samples,
        /// which is how an upload leaves it; `map` is met undefined and left the same way, ready
        /// for the array's sampler.
        ///
        /// The size is the image's own, which for a texture held to a smaller side is a level
        /// further down the file.
        ///
        /// @param punchThrough whether `source` is a BC1 file, whose blocks store black where
        ///        nothing was painted.
        void record(VkCommandBuffer commands, const Image& source, const Image& map, bool punchThrough) const;

    private:
        ComputePipeline mSum;
        ComputePipeline mMap;

        /// Every cell's sum, from the one stage to the other. One for every texture in turn:
        /// textures arrive one after another in one recording, and a barrier between the last
        /// map and the next sum is what a buffer a texture costs nothing.
        Buffer mSums;
    };
}
