#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <components/rtx/renderer.hpp>

#include "atrouspass.hpp"
#include "buffer.hpp"
#include "commands.hpp"
#include "compositepass.hpp"
#include "device.hpp"
#include "exposurepass.hpp"
#include "instance.hpp"
#include "tonepass.hpp"

namespace Rtx
{
#ifdef OPENMW_RTX_DLSS
    class Dlss;
    class DlssPass;
#endif
    class GBuffer;
    class Image;
    class SceneAcceleration;
    class SceneBuffers;
    class TextureArray;
    class VisibilityPass;

    /// `Renderer` over Vulkan.
    ///
    /// This is the shot command and the pixel tests' fixture merged: both stood up a device, built a
    /// scene on it, traced into an image and read it back, and both are now one implementation that
    /// cannot disagree with itself.
    class VulkanRenderer final : public Renderer
    {
    public:
        /// Throws `Error` where this machine cannot run it. `createVulkanRenderer` is what turns
        /// that into a reason a caller can act on.
        explicit VulkanRenderer(const RendererOptions& options);
        ~VulkanRenderer() override;

        std::string describeDevice() const override;
        bool isValidating() const override;
        void setScene(const SceneDesc& scene, std::span<const TextureData> textures, const SeaState& sea) override;
        const SceneStats& getSceneStats() const override { return mStats; }
        void resize(std::uint32_t width, std::uint32_t height) override;
        FrameExtents getExtents() const override;
        FrameResult renderFrame(const Shaders::VisibilityConstants& camera, const FrameOptions& options) override;
        void readPixels(std::vector<std::uint8_t>& pixels) override;
        void readChannel(Channel channel, std::vector<float>& values) override;
        void takeValidationErrors(std::vector<std::string>& errors) override;

    private:
        /// @param width, height what the frame is **presented** at. What it is traced at is the
        ///        upscaler's answer for that, or the same numbers where nothing upscales.
        void createTargets(std::uint32_t width, std::uint32_t height);

        // Declaration order is destruction order reversed, and everything below the device is built
        // on it.
        Instance mInstance;
        Device mDevice;
        CommandPool mPool;

        std::filesystem::path mShaderDirectory;

        /// Fixed at construction: an upscaler is brought up once, and there is nothing to switch
        /// to at runtime that would not mean rebuilding every target anyway.
        Upscale mUpscale = Upscale::Off;

        /// What the trace runs at, and so what every G-buffer channel and the composite are sized
        /// to. Equal to the output extent wherever nothing upscales.
        std::uint32_t mRenderWidth = 0;
        std::uint32_t mRenderHeight = 0;

        std::uint32_t mOutputWidth = 0;
        std::uint32_t mOutputHeight = 0;

        /// The composite's output at the render extent: one frame in linear radiance, before
        /// anything upscales it and before the display curve.
        std::unique_ptr<Image> mColour;

        /// The frame as bytes at the output extent, which is what anything outside this reads.
        std::unique_ptr<Image> mTarget;

        /// The running sum, and null until a frame asks to be averaged into one.
        ///
        /// **Whether it exists is also whether anything has written it**, because the frame that
        /// makes one is the frame that fills it: the first write needs no contents and nothing to
        /// wait on, and every one after reads what the last left — a hazard across submits that the
        /// fence orders and does not make visible.
        std::unique_ptr<Image> mHistory;

        /// What the trace writes and the composite reads: one frame's light, still in pieces.
        std::unique_ptr<GBuffer> mChannels;

        /// The camera the last frame was traced with, for reprojecting this one against.
        ///
        /// Its basis is all zero until a frame has been traced, and after a resize or a new scene —
        /// which the shader reads as "there is no previous frame" and answers with no motion at all.
        Shaders::VisibilityConstants mPreviousCamera{};

        Buffer mHitCount;

        // Rebuilt by `setScene`, in dependency order: the buffers borrow the structures' indices and
        // the pass names the texture array's layout.
        std::unique_ptr<SceneAcceleration> mAcceleration;
        std::unique_ptr<SceneBuffers> mBuffers;
        std::unique_ptr<TextureArray> mTextures;
        std::unique_ptr<VisibilityPass> mPass;
        SceneStats mStats;

        /// Held by value rather than built with the scene, because they depend on neither the
        /// scene nor the size of the image: what they read is pushed at record time. The filter is
        /// not const only because it keeps a channel the size of the frame.
        AtrousPass mFilter;
        CompositePass mComposite;
        ExposurePass mExposure;
        TonePass mTone;

#ifdef OPENMW_RTX_DLSS
        /// NGX, brought up only where something asked to be upscaled — it is global to the process
        /// and keyed by device, so it is owned rather than made and dropped per question.
        std::unique_ptr<Dlss> mNgx;

        /// Ray Reconstruction, built for one pair of resolutions and so rebuilt by every resize.
        std::unique_ptr<DlssPass> mUpscaler;

        /// What it writes: the frame at the output extent, still in linear radiance.
        std::unique_ptr<Image> mUpscaled;

        /// **Zero, and a whole image of it.** Ray Reconstruction demodulates the specular lobe out
        /// of the colour it is given, and this renderer has no specular lobe to demodulate — no
        /// material model, no roughness, no F0. Saying zero is the honest answer; saying nothing is
        /// not an option, because the input is not one NGX treats as optional.
        std::unique_ptr<Image> mNoSpecular;
#endif
    };

    /// Builds a Vulkan renderer, or nothing where this machine has no driver that qualifies.
    std::unique_ptr<Renderer> createVulkanRenderer(const RendererOptions& options, std::string& reason);
}
