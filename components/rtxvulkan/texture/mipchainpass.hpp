#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/mipchain.h>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

namespace Rtx
{
    class Device;
    class Image;

    /// The chain a file did not carry, made on the device as the texture arrives: `mipchain.comp`,
    /// which says why it is here and not on the host. One dispatch a level, the first fetching the
    /// upload and every other boxing the level above it.
    class MipChainPass
    {
    public:
        explicit MipChainPass(const Device& device);

        /// Records level `level` of `chain` from `source`: the first is `source` texel for texel and
        /// every other the box over the one above. `source` is one level, met as a texture the
        /// trace samples, which is how an upload leaves it, and in a format with no curve under it,
        /// so a fetch is the stored bytes; `chain` is met where a dispatch reads and writes it,
        /// with a storage view a level in a format with no curve under it either. The level above
        /// is ordered against this one by the caller, which `TextureArrival` does for a level of
        /// every chain at once.
        ///
        /// @param encoded whether `chain`'s own format is display-encoded, so the box averages in
        ///        light and writes back encoded.
        void recordLevel(
            VkCommandBuffer commands, const Image& source, const Image& chain, std::uint32_t level, bool encoded) const;

    private:
        ComputePipeline<Shaders::MipChainConstants> mPipeline;
    };
}
