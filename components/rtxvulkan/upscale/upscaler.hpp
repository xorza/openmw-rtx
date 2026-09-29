#pragma once

#include <memory>
#include <string>

#include <osg/Vec2f>
#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>

namespace Rtx
{
    class Device;
    class Image;

    /// Everything one reconstruction reads: what a temporal upscaler takes, and no guide one
    /// upscaler alone asks for. Every image is created with `VK_IMAGE_USAGE_SAMPLED_BIT`, since an
    /// upscaler may sample any of them.
    struct UpscaleInputs
    {
        /// The frame at render resolution, as the trace chain composed it: denoised by the wavelet
        /// where it ran, and the trace's own composite where nothing filtered it.
        const Image& mColour;

        /// `CHANNEL_SURFACE`: the distance from the eye along each pixel's ray in `g`. An upscaler
        /// that compares depths derives the one a rasterizer would have written from it, on the
        /// frames it upscales, rather than every frame storing one for it.
        const Image& mSurface;

        /// Where each surface stood on the previous frame's screen, less where it stands now, in
        /// render pixels.
        const Image& mMotion;

        /// Where inside its pixel this frame sampled, in render pixels — the same offset the trace
        /// was given.
        osg::Vec2f mJitter;

        /// How long since the previous frame, in milliseconds, or nought where there was none: a
        /// motion vector says how far something went and not how fast.
        float mFrameDeltaMs = 0.0f;

        /// Whether the previous frame is worth anything. True after a jump no motion vector can
        /// describe: a new cell, a teleport, the first frame after a resize.
        bool mReset = false;
    };

    /// What the frame asks of whatever reconstructs it across frames: what to trace at, and the
    /// frame at the output extent. None stands behind it yet, so `makeUpscaler` refuses by name,
    /// and nothing else in the frame path knows whether one does.
    class Upscaler
    {
    public:
        virtual ~Upscaler() = default;

        Upscaler(const Upscaler&) = delete;
        Upscaler& operator=(const Upscaler&) = delete;

        /// Builds what the upscaler keeps for one pair of extents and the image it writes, releasing
        /// the last, so nothing is left behind for a pair that may not come back. Once per
        /// resolution, and never per frame.
        virtual void resize(VkExtent2D render, VkExtent2D output, const Upscaling& how) = 0;

        /// Lets what `resize` built go and keeps the upscaler, for a mode turned off that may not come
        /// back.
        virtual void release() = 0;

        /// The image `record` writes, which the display composites the puffs over and maps.
        virtual const Image& getOutput() const = 0;

        /// Records one reconstruction into `getOutput`, at the output extent, and leaves it in
        /// `Use::sAnyGeneralWrite`: what the reconstruction recorded is its own, and nothing here
        /// knows which stages it used. After `resize`.
        virtual void record(VkCommandBuffer commands, const UpscaleInputs& inputs) = 0;

    protected:
        Upscaler() = default;
    };

    /// Brings the upscaler up, or throws `Unsupported` naming what is missing.
    std::unique_ptr<Upscaler> makeUpscaler(const Device& device, VkInstance instance);

    /// One line for `info`: which upscaler this renderer has, or none.
    std::string describeUpscaling(const Device& device, VkInstance instance);
}
