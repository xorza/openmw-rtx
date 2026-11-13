#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <components/rtx/common/cardaddress.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/framepast.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/framedescriber.hpp>
#include <components/rtx/renderer/guirenderer.hpp>
#include <components/rtx/renderer/memoryreport.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/renderer/verticalsync.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/memory/growablebuffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>
#include <components/rtxvulkan/display/displaychain.hpp>
#include <components/rtxvulkan/gui/guidrawer.hpp>
#include <components/rtxvulkan/present/presenttarget.hpp>
#include <components/rtxvulkan/scene/scenepasses.hpp>
#include <components/rtxvulkan/scene/sceneslots.hpp>
#include <components/rtxvulkan/texture/texture.hpp>
#include <components/rtxvulkan/texture/texturearrival.hpp>
#include <components/rtxvulkan/trace/stresspass.hpp>
#include <components/rtxvulkan/trace/tracechain.hpp>
#include <components/rtxvulkan/trace/tracemedia.hpp>
#include <components/rtxvulkan/trace/tracepasses.hpp>
#include <components/rtxvulkan/upscale/dlssapplication.hpp>
#include <components/rtxvulkan/upscale/upscaler.hpp>

#include "framering.hpp"
#include "frametracer.hpp"
#include "picturetracer.hpp"

namespace Rtx::Testing
{
    class RendererProbe;
}

namespace Rtx
{
    class Presenter;
    class Surface;

    /// `Renderer` over Vulkan.
    class VulkanRenderer final : public Renderer
    {
    public:
        /// Throws `Unsupported` where this machine cannot run it and `DeviceError` where the device
        /// failed. `createVulkanRenderer` is how a host makes one.
        explicit VulkanRenderer(const RendererOptions& options);
        ~VulkanRenderer() override;

        /// Pinned: its passes hold references to its device and to one another.
        VulkanRenderer(const VulkanRenderer&) = delete;
        VulkanRenderer& operator=(const VulkanRenderer&) = delete;
        VulkanRenderer(VulkanRenderer&&) = delete;
        VulkanRenderer& operator=(VulkanRenderer&&) = delete;

        std::string describeDevice() const override;
        CardAddress getCardAddress() const override;
        bool isValidating() const override;

        void setScene(SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> textures) override;
        void extendScene(SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> arrived) override;
        SceneHeld describeHeld(SceneSlot slot) const override;
        std::span<const Refusal> getRefusals(SceneSlot slot) const override;
        void dropTextures(SceneSlot slot, std::span<const Index> textures) override;
        void placeScene(SceneSlot slot, const SceneDesc& scene) override;
        const SceneStats& getSceneStats() const override { return mStats; }
        MemoryReport getMemoryReport() const override;
        void resize(std::uint32_t width, std::uint32_t height) override;
        void showIn(std::uint32_t width, std::uint32_t height) override;
        void setUpscale(Upscale upscale) override;

        void setVerticalSync(VerticalSync mode) override;
        void setAnisotropy(std::uint32_t anisotropy) override;
        void setGamma(float gamma) override;
        void setIndirectLighting(bool traced) override;
        void setBounceRays(std::uint32_t rays) override;
        void skipFrame() override;
        FrameExtents getExtents() const override;
        const RenderProfile& getProfile() const override { return mProfile; }
        std::span<const UpscalerChoice> getUpscalers() const override { return mUpscalers; }
        UpscalerInput getUpscalerInput() const override { return mUpscaler.getInput(); }
        JobProgress awaitKernels(KernelSet set, std::chrono::milliseconds patience) override;
        FrameTraced renderFrame(const FrameRequest& request) override;
        void holdAir(const AirClock& air) override;
        std::uint64_t getFrameCount() const override;
        std::optional<FrameResult> finishFrame() override;
        std::optional<FrameResult> collectFrame() override;
        void presentFrame() override;
        void awaitPresented() override;

        SceneSlot addViewScene() override;
        void reservePicture(std::uint32_t width, std::uint32_t height) override;
        void dropViewScene(SceneSlot scene) override;

        GuiSlot addGuiTexture(std::uint32_t width, std::uint32_t height) override;
        std::span<std::uint8_t> lendGuiTexture(GuiSlot texture, const GuiRegion& region) override;
        void sendGuiTexture(GuiSlot texture) override;
        void dropGuiTexture(GuiSlot texture) override;
        void drawGui(std::span<const GuiVertex> vertices, std::span<const GuiBatch> batches) override;
        bool traceGuiTexture(
            GuiSlot texture, SceneSlot scene, const Viewpoint& view, const GuiTraceOptions& options) override;
        bool takeGuiCopy(GuiSlot texture, std::span<std::uint8_t> into) override;
        void finishGuiTraces() override;
        void readPixels(std::vector<std::uint8_t>& pixels) override;
        void readDeepPixels(std::vector<std::uint16_t>& samples) override;
        std::optional<NotFiniteFrame> takeNotFiniteFrame() override;

    private:
        /// What a test asks of this backend and a game never does, defined in the test tree, so
        /// none of it ships.
        friend class Testing::RendererProbe;

        /// A frame traced from a block written whole: what `renderFrame` hands on once it has described
        /// the world, and what a test of one field traces directly. Every field `sampleFrame` writes is
        /// left at nought.
        void traceFrame(const Shaders::VisibilityConstants& constants, const FrameOptions& options,
            const WorldOptions& described = {});

        /// The same, for a picture inside the interface, from a block the viewpoint's
        /// `traceGuiTexture` lays its options over.
        bool tracePicture(GuiSlot texture, SceneSlot scene, const Shaders::VisibilityConstants& camera,
            const GuiTraceOptions& options);

        /// @param width, height what the frame is presented at. What it is traced at is the
        ///        upscaler's answer for the mode, `Upscaler::extentsFor`.
        void createTargets(std::uint32_t width, std::uint32_t height);

        /// The one way the queue is emptied: everything given to it and everything waiting finished,
        /// everything buried let go. The order is load-bearing — the interface's batch and the deferred
        /// one first, since they ride the next submit and nothing else will make one; then the frames in
        /// flight; then the device, for the presenter's submits the ring does not count; the graveyard
        /// last, once nothing can be reading.
        void drain();

        /// Whether a frame is upscaled, which is the mode alone: the upscaler's pipelines are built
        /// with the renderer whatever the mode, and `createTargets` makes its images where a mode
        /// upscales and lets them go where it does not.
        bool upscaling() const { return upscales(mProfile.mUpscale); }

        // Declaration order is destruction order reversed, and everything below the device is built
        // on it.

        /// What NGX is told of the application, where the run may use DLSS: ahead of the instance,
        /// whose extensions it is asked for, and outliving everything NGX is handed it with.
        std::optional<Dlss::Application> mDlss;

        Instance mInstance;

        /// The window's surface, or null for no window: before the device, which is chosen for a
        /// queue that presents to it, and handed to the presenter, its one owner from then on.
        std::unique_ptr<Surface> mSurface;

        Device mDevice;

        /// Before the trace's passes, whose kernels are specialized on the masks it reads, and before the
        /// menu, which offers DLSS by what its attempt came to. Its pipelines are made whatever the mode,
        /// so the mode and not this says whether a frame is upscaled.
        Upscaler mUpscaler;

        /// `getUpscalers`' answer, a prefix of `sUpscalerMenu`: DLSS where it runs or was not tried and
        /// every fact for it holds (`offersDlss`).
        std::span<const UpscalerChoice> mUpscalers;

        /// Whether the frame this builds counts for the host. `RendererOptions::mReporting` says why
        /// the game's does not.
        bool mCounting = false;

        /// Whether `awaitKernels` has seen the start's compile whole and saved the pipeline cache.
        bool mKernelsSaved = false;

        /// Whether `awaitKernels` has seen the set it was asked for whole and logged the pipelines
        /// made by then (`Device::logPipelineSummary`).
        bool mKernelsSummarised = false;

        /// The frame `createTargets` last logged.
        std::string mFrameLogged;

        /// What the run decided once, read where each knob is used. `mUpscale` is a setting and not a frame
        /// option because `setUpscale` rebuilds every target; what a frame carries (the reconstruction
        /// request, the exposure, the sample) is read off its own blocks instead.
        RenderProfile mProfile;

        /// One over `mProfile.mGamma`, which the display curve is handed: worked out where the
        /// gamma is set rather than on every frame.
        float mInverseGamma = 1.0f;

        /// The hold `RenderProfile::mStressOverlapMs` asked for, or nothing. Ahead of the ring, which
        /// reads the hold's reading in milliseconds by the rate the hold measured.
        std::unique_ptr<StressPass> mStress;

        /// The frames in flight and what each came to.
        FrameRing mRing;

        /// The frame as bytes at the output extent, which is what anything outside this reads, and the
        /// one place that extent is stated (`PresentTarget::getExtent`), so no pair of members must be
        /// kept level.
        PresentTarget mTarget;

        /// Where `readPixels` and `readDeepPixels` land the picture, grown at the first and kept, so a run
        /// that writes a picture at every stop allocates once.
        GrowableBuffer mReadBack;

        /// Before the trace's passes and the display, which read the scenes' texture layout.
        ScenePasses mScenePasses;

        /// What every scene's arriving textures are recorded through, handed to each at the call: its
        /// rooms are scratch grown to the largest group any scene met, not one copy per scene.
        TextureArrival mArrival;

        /// Whether a scene was extended since the last frame was traced, which staged what it
        /// brought: the frame after keeps the ring as it is.
        bool mHandedOver = false;

        /// The passes every trace runs, whichever camera it is for, before the two chains that
        /// hold them.
        TracePasses mTracePasses;

        /// What the frame is traced into, at the render extent — which is the output extent
        /// wherever nothing upscales.
        FrameTracer mFrame;

        /// What each frame is described with and carries to the next: the air's drift, what this
        /// renderer's own events cost the next traced frame — a new extent, a new world, the first
        /// frame of all — and the camera the last frame was traced with.
        FrameDescriber mDescriber;

        SceneStats mStats;

        /// Everything between a finished trace and a target, for the frame and for every picture
        /// inside the interface.
        DisplayChain mDisplay;

        TraceMedia mMedia;

        /// After the passes above, which every scene holds by reference.
        SceneSlots mScenes;

        /// `SceneDesc::getIdentity` of the world a change of mode released for its reserve, until
        /// the hand-over builds it again: that build is the same world, which keeps every history a
        /// new world loses. Nought where none waits.
        std::uint64_t mReleasedWorld = 0;

        GuiDrawer mGui;

        /// After the media, the display and the interface, which it holds by reference.
        PictureTracer mPictures;

        /// What the display handed back of the last frame, which the tests read
        /// (`Testing::RendererProbe::readShownRadiance`): the composed frame, or the canvas the puffs
        /// went over.
        const Image* mShownRadiance = nullptr;

        /// The room `reservePicture` was asked for, the largest of every side asked, which
        /// `createTargets` makes the pictures' chain at. Nought until a view asks.
        VkExtent2D mPictureRoom{ 0, 0 };

        /// `RunProfile::mReservedUpscales`, which `createTargets` reserves the frame's room for.
        std::vector<Upscale> mReservedUpscales;

        /// Null where nothing asked for a window. After `mTarget`, so it is destroyed before it:
        /// its command buffers blit out of the image, and its destructor waits for the device to
        /// finish them, which `VUID-vkDestroyImage-image-01000` asks of every submitted command that
        /// names an image before the image goes.
        std::unique_ptr<Presenter> mPresenter;
    };
}
