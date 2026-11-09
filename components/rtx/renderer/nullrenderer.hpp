#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <components/rtx/common/cardaddress.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/verticalsync.hpp>
#include <components/rtx/scene/refusal.hpp>

#include "framesinflight.hpp"
#include "guirenderer.hpp"
#include "memoryreport.hpp"
#include "picturerenderer.hpp"
#include "renderer.hpp"
#include "sceneholder.hpp"
#include "slot.hpp"

namespace Rtx
{
    /// **A renderer that keeps every promise and draws nothing**, with no graphics API under it: the
    /// game's ray tracer, its mirror, its ring, its interface and its pictures run whole above it,
    /// on a machine with no device — CI's runners, where upstream's integration tests drive the
    /// game. The null RHI of Unreal (`-nullrhi`) and Godot's headless display server, at the one
    /// seam this tree has for it. What a picture comes to is black: `readPixels` and `readDeepPixels`
    /// answer a frame of noughts, and `takeGuiCopy` a copy of noughts once a trace left one.
    class NullRenderer final : public Renderer
    {
    public:
        explicit NullRenderer(const RendererOptions& options);

        void setScene(SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> textures) override;
        void extendScene(SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> arrived) override;
        SceneHeld describeHeld(SceneSlot slot) const override;
        std::span<const Refusal> getRefusals(SceneSlot) const override { return {}; }
        void dropTextures(SceneSlot, std::span<const Index>) override {}
        void placeScene(SceneSlot slot, const SceneDesc& scene) override;
        SceneSlot addViewScene() override;
        void dropViewScene(SceneSlot slot) override;

        void reservePicture(std::uint32_t, std::uint32_t) override {}
        bool traceGuiTexture(
            GuiSlot texture, SceneSlot scene, const Viewpoint& view, const GuiTraceOptions& options) override;
        bool takeGuiCopy(GuiSlot texture, std::span<std::uint8_t> into) override;
        void finishGuiTraces() override {}

        FrameExtents getExtents() const override { return mExtents; }
        GuiSlot addGuiTexture(std::uint32_t width, std::uint32_t height) override;
        std::span<std::uint8_t> lendGuiTexture(GuiSlot texture, const GuiRegion& region) override;
        void sendGuiTexture(GuiSlot texture) override;
        void dropGuiTexture(GuiSlot texture) override;
        void drawGui(std::span<const GuiVertex>, std::span<const GuiBatch>) override {}

        void resize(std::uint32_t width, std::uint32_t height) override;
        void showIn(std::uint32_t, std::uint32_t) override {}
        void setUpscale(Upscale upscale) override;
        void setVerticalSync(VerticalSync) override {}
        void setAnisotropy(std::uint32_t anisotropy) override { mProfile.mAnisotropy = anisotropy; }
        void setGamma(float gamma) override { mProfile.mGamma = gamma; }
        void setIndirectLighting(bool traced) override;
        void setBounceRays(std::uint32_t rays) override;
        JobProgress awaitKernels(KernelSet, std::chrono::milliseconds) override { return {}; }
        FrameTraced renderFrame(const FrameRequest& request) override;
        void holdAir(const AirClock&) override {}
        void skipFrame() override;
        std::uint64_t getFrameCount() const override { return mFrames; }
        std::optional<FrameResult> finishFrame() override;
        std::optional<FrameResult> collectFrame() override;
        void presentFrame() override {}
        void awaitPresented() override {}
        std::string describeDevice() const override;
        CardAddress getCardAddress() const override { return {}; }
        const RenderProfile& getProfile() const override { return mProfile; }
        std::span<const UpscalerKind> getUpscalers() const override { return offeredUpscalers(false); }
        bool isValidating() const override { return false; }
        MemoryReport getMemoryReport() const override { return {}; }
        const SceneStats& getSceneStats() const override { return mStats; }
        void readPixels(std::vector<std::uint8_t>& pixels) override;
        void readDeepPixels(std::vector<std::uint16_t>& samples) override;

    private:
        /// What a GUI slot holds: whether it is handed out, and whether a trace into it left a copy,
        /// which stands, as a device's does, until the slot is dropped.
        struct GuiTexture
        {
            std::uint32_t mWidth = 0;
            std::uint32_t mHeight = 0;
            bool mHeld = false;
            bool mCopied = false;
        };

        /// The frame a report will hand back, and how many frames the ring held when it closed.
        struct Unreported
        {
            std::uint64_t mFrame = 0;
            std::uint32_t mInFlight = 0;

            /// What reconstructed it, resolved when it was traced (`RenderProfile::reconstructionFor`).
            Reconstruction mReconstruction{};
        };

        bool holds(SceneSlot slot) const;
        SceneHeld& heldAt(SceneSlot slot);
        const SceneHeld& heldAt(SceneSlot slot) const;
        GuiTexture& textureAt(GuiSlot slot);

        /// What `finishFrame` and `collectFrame` hand back of `frame`.
        FrameResult resultOf(const Unreported& frame) const;

        RenderProfile mProfile;
        FrameExtents mExtents;
        SceneStats mStats;

        SceneHeld mWorld;

        /// Every view scene's, by its index, and the indices given back for the next to take.
        std::vector<std::optional<SceneHeld>> mViews;
        std::vector<std::uint32_t> mFreeViews;

        std::vector<GuiTexture> mGuiTextures;
        std::vector<std::uint32_t> mFreeGuiTextures;

        /// What `lendGuiTexture` hands out, grown to the largest rectangle lent and never shrunk.
        std::vector<std::uint8_t> mLending;
        std::optional<GuiSlot> mLent;

        /// The frames `renderFrame` closed and no report has handed back yet, oldest first: at most
        /// `sFramesInFlight`, as a backend's ring holds them.
        std::array<Unreported, sFramesInFlight> mUnreported{};
        std::uint32_t mUnreportedCount = 0;

        std::uint64_t mFrames = 0;

        /// Whether a placement of the world opened the frame, which `renderFrame` and `skipFrame`
        /// close: a backend's frame begins where the world is first placed in it.
        bool mOpen = false;
    };
}
