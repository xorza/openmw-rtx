#pragma once

#include <vulkan/vulkan_core.h>

#include <osg/Vec2f>

#include "dlss.hpp"

// NGX's own, forward-declared for the reason `dlss.hpp` gives.
struct NVSDK_NGX_Handle;

namespace Rtx
{
    class Image;

    /// Everything one evaluation reads, and the one image it writes.
    ///
    /// **Every image must have been created with `VK_IMAGE_USAGE_SAMPLED_BIT`.** DLSS samples its
    /// inputs; one it cannot sample reads as zero, NGX returns success and the validation layers say
    /// nothing, so the whole frame comes back black with nothing pointing at the cause. `record`
    /// asserts it rather than leaving it to be found.
    struct DlssInputs
    {
        /// The trace's radiance at render resolution, **undenoised**. Ray Reconstruction is the
        /// denoiser: handing it a filtered frame is asking it to reconstruct detail already blurred
        /// away.
        const Image& mColour;

        const Image& mDiffuseAlbedo;
        const Image& mSpecularAlbedo;

        /// Shading normal in `xyz`, roughness in `w` — the feature is built for the packed layout,
        /// which is one resource fewer to write and to bind.
        const Image& mNormalRoughness;

        /// Clip depth, in the sense a rasterizer would have written it.
        const Image& mDepth;

        /// Where each surface stood on the previous frame's screen, less where it stands now, in
        /// render pixels.
        const Image& mMotion;

        /// The upscaled frame, at output resolution.
        const Image& mOutput;

        /// Where inside its pixel this frame sampled, in render pixels — the same offset the trace
        /// was given.
        osg::Vec2f mJitter;

        /// Whether the previous frame is worth anything. True after a jump no motion vector can
        /// describe: a new cell, a teleport, the first frame after a resize.
        bool mReset = false;
    };

    /// DLSS Ray Reconstruction, built for one pair of resolutions.
    ///
    /// **The feature and the parameter map it was built from are one thing.** That map is not the
    /// capability map NGX answers questions from — it is allocated per feature, it has to outlive
    /// the feature, and the two are released together and in that order.
    ///
    /// Sized at construction, so a resolution change means a new one.
    class DlssPass
    {
    public:
        /// Builds Ray Reconstruction to take `render` and produce `output`.
        ///
        /// @param commands must be recording, and must be submitted and waited on before the first
        ///        evaluation: NGX uploads the network's weights here. Once per resolution, not once
        ///        per frame.
        ///
        /// Throws `Error` where NGX will not build it.
        DlssPass(const Dlss& ngx, VkCommandBuffer commands, VkExtent2D render, VkExtent2D output, Upscale upscale);
        ~DlssPass();

        DlssPass(const DlssPass&) = delete;
        DlssPass& operator=(const DlssPass&) = delete;

        /// Records one upscale. Every image must be in `VK_IMAGE_LAYOUT_GENERAL` and hold this
        /// frame. Throws `Error` where NGX refuses the evaluation.
        void record(VkCommandBuffer commands, const DlssInputs& inputs) const;

        VkExtent2D getRenderExtent() const { return mRenderExtent; }
        VkExtent2D getOutputExtent() const { return mOutputExtent; }

    private:
        NVSDK_NGX_Handle* mHandle = nullptr;
        NVSDK_NGX_Parameter* mParameters = nullptr;
        VkExtent2D mRenderExtent{};
        VkExtent2D mOutputExtent{};
    };
}
