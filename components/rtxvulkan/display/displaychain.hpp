#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/sunglare.hpp>
#include <components/rtx/scene/debuglines.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/growablebuffer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/trace/sunglarepass.hpp>

#include "bloompass.hpp"
#include "exposurepass.hpp"
#include "linepass.hpp"
#include "tonepass.hpp"

namespace Rtx
{
    class Buffer;
    class Device;
    class GBuffer;
    class GpuTimer;
    class Image;
    class VisibilityPass;
    struct TraceResult;

    /// What a frame asks of the display chain beyond what a picture inside the interface does:
    /// a picture has no lens, no eye of its own, no share of the sun, no lines over it and no
    /// timer, and cannot be handed any of them. Nothing here is held.
    struct FrameLook
    {
        /// The eye adapts off the shown frame at its own rate — from nothing where `mReset` says the
        /// camera has no past — or is held at a value, or keeps the one the frame before ended on. A picture is
        /// measured off nothing, which `ExposurePass::getPictureExposure` says is a buffer of its own.
        struct Measured
        {
            float mSeconds;
            bool mReset;
            float mBias;
        };
        struct Fixed
        {
            float mValue;
        };
        struct Held
        {
        };
        using Exposure = std::variant<Measured, Fixed, Held>;
        Exposure mExposure;

        /// The sun glare fader and the sun's share, eased at the query's own rate.
        struct Glare
        {
            SunGlare mFader;
            float mSeconds;
            bool mReset;
        };
        Glare mGlare;

        /// The debug modes' lines and triangles over the picture, and the slot's own buffer they
        /// are drawn from: the frame behind read its own slot's, so nothing here is written under
        /// a submit.
        DebugLines mDebug;
        GrowableBuffer& mDebugVertices;

        GpuTimer& mTimer;
    };

    /// What one picture asks of the display chain: a frame, or a picture inside the interface,
    /// which is the same list with no `FrameLook`. Nothing here is held.
    struct Display
    {
        /// What the trace bound and the sprite tile list it read.
        const TraceResult& mTrace;

        /// The frame as it will be shown: the puffs go over it and the curve maps it. The
        /// upscaler's output where one runs, the trace's own composite where none does, and a
        /// picture's own colour inside the interface.
        const Image& mShown;

        /// Where the last writer of `mShown` left it, which the display hands over from: the
        /// trace's `Use::sAnyGeneralRead`, or the upscaler's `Use::sAnyGeneralWrite`.
        ImageUse mShownFrom;

        /// How much of the shown frame the picture is, from its corner: the whole of a frame's,
        /// and a picture's own size inside an image that may be larger. The curve encodes as much
        /// of `mTarget` from its corner.
        VkExtent2D mExtent;

        /// The camera the trace sampled, which the curve and the lines are told.
        const Shaders::VisibilityConstants& mSampled;

        /// What the curve writes into, at least `mExtent` large. Its contents are discarded, because
        /// the curve rewrites it whole.
        Image& mTarget;

        /// Nothing for a picture inside the interface, which is a diagram: measured off nothing,
        /// mapped with no glare, spread by no lens, and not timed.
        std::optional<FrameLook> mFrame;
    };

    /// What comes after the trace and the upscaler: the puffs over the picture, the lens, the eye,
    /// the sun's share, the curve and the lines over the result. One chain for the frame and for
    /// every picture inside the interface, which differ in what they ask of it and in nothing
    /// else, so what happens between a finished trace and a target cannot be told two ways.
    class DisplayChain
    {
    public:
        /// @param puffs the trace's own pass, which composites the sprites over what was traced.
        /// @param textureLayout the scene's bindless textures, which the curve samples the star
        ///        sheet out of.
        DisplayChain(const Device& device, const VisibilityPass& puffs, VkDescriptorSetLayout textureLayout);

        /// The lens over `width` by `height`, which is what the frame is by the time the curve
        /// maps it: the upscaler's output where one runs and the trace's own extent where none
        /// does. A pyramid built at the other extent is a bloom at the wrong scale.
        void resize(std::uint32_t width, std::uint32_t height);

        /// The glare fader's query starts the frame at nothing, ahead of the trace that counts.
        void beginGlare(VkCommandBuffer commands) const;

        /// The two counts the eye's launch adds to, `SunGlarePass::getCounts`.
        const Buffer& getGlareCounts() const { return mSunGlare.getCounts(); }

        /// Says the measured exposure and the glare's eased share are worthless, each until the next
        /// frame that eases it: the share eases on every frame, and the exposure only on a frame
        /// that measures.
        void resetHistory() { mExposureStale = mGlareStale = true; }

        /// Records everything from the puffs to the target, and leaves `what.mTarget` in
        /// `Use::sComputeWrite`, where the curve left it.
        void record(VkCommandBuffer commands, const Display& what);

    private:
        /// Draws `look.mDebug` over the target after the curve, from the slot's own vertex
        /// buffer, depth-tested against the channels. Nothing at all for a frame with none,
        /// which is nearly every frame.
        void recordDebugLines(VkCommandBuffer commands, const Display& what, const FrameLook& look);

        const VisibilityPass& mPuffs;

        BloomPass mBloom;
        ExposurePass mExposure;

        /// How much of the sun's quad the frame's rays could see, eased, which the frame's curve
        /// lays the glare fader over the picture by.
        SunGlarePass mSunGlare;
        TonePass mTone;

        /// The debug modes' lines and triangles, over the picture and under the interface.
        LinePass mLines;

        /// Set by `resetHistory` and each spent by the next record that eases its history.
        bool mExposureStale = false;
        bool mGlareStale = false;
    };
}
