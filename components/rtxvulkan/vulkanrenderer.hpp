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
#include "instance.hpp"

namespace Rtx
{
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
        FrameResult renderFrame(const Shaders::VisibilityConstants& camera, const FrameOptions& options) override;
        void readPixels(std::vector<std::uint8_t>& pixels) override;
        void takeValidationErrors(std::vector<std::string>& errors) override;

    private:
        void createTargets(std::uint32_t width, std::uint32_t height);

        // Declaration order is destruction order reversed, and everything below the device is built
        // on it.
        Instance mInstance;
        Device mDevice;
        CommandPool mPool;

        std::filesystem::path mShaderDirectory;

        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
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
    };

    /// Builds a Vulkan renderer, or nothing where this machine has no driver that qualifies.
    std::unique_ptr<Renderer> createVulkanRenderer(const RendererOptions& options, std::string& reason);
}
