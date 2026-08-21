#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include "image.hpp"

namespace Rtx
{
    class Device;

    /// What the trace leaves behind, before anything has decided what the picture looks like.
    ///
    /// **A picture cannot be filtered and these can.** One bounce per pixel is noisy, and the only
    /// thing that removes noise without removing detail is a blur that runs over the light alone —
    /// which means the light has to still be separate from the surface it landed on when the blur
    /// reaches it. By the time a pixel is a colour, the albedo has been multiplied in, the fog has
    /// been laid over it and the curve has been applied; there is nothing left to filter that would
    /// not also smear the wall's texture.
    ///
    /// So the trace writes what it knows in the form the next pass can use, and the composite puts
    /// it back together:
    ///
    ///     colour = direct + modulate * filter(indirect)
    ///
    /// **And this is the same buffer M10 needs.** DLSS Ray Reconstruction asks for exactly this —
    /// demodulated radiance, the albedo to put back, normals and depth — so the split earns its
    /// place twice over even if the filter written on top of it is later replaced.
    class GBuffer
    {
    public:
        GBuffer(const Device& device, std::uint32_t width, std::uint32_t height);

        /// Direct light, emission, the sky and water, with the fog already over all of it.
        const Image& getDirect() const { return mDirect; }

        /// One bounce with the albedo divided out. The only channel a filter may touch.
        const Image& getIndirect() const { return mIndirect; }

        /// The albedo, times whatever the water and the air took off the way to the eye.
        const Image& getModulate() const { return mModulate; }

        /// The shading normal in `xyz` and the hit distance in `w`, for telling edges apart.
        const Image& getGuide() const { return mGuide; }

        std::uint32_t getWidth() const { return mDirect.getWidth(); }
        std::uint32_t getHeight() const { return mDirect.getHeight(); }

        /// Discards the contents and makes all four writable, which is how a frame starts.
        ///
        /// Waits for the previous frame's composite to have read them, so that one set of channels
        /// can serve a window that keeps several frames in flight.
        void begin(VkCommandBuffer commands) const;

        /// Orders the pass that wrote them against the pass about to read them.
        void handOver(VkCommandBuffer commands) const;

    private:
        Image mDirect;
        Image mIndirect;
        Image mModulate;
        Image mGuide;
    };
}
