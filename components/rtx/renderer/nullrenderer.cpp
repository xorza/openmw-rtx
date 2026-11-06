#include "nullrenderer.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/scene/scenedesc.hpp>

namespace Rtx
{
    NullRenderer::NullRenderer(const RendererOptions& options)
        : mProfile(options.mRun.mProfile)
        , mExtents(extentsFor(options.mWidth, options.mHeight, options.mRun.mProfile.mUpscale))
    {
    }

    bool NullRenderer::holds(const SceneSlot slot) const
    {
        return slot.isWorld() || (slot.getViewIndex() < mViews.size() && mViews[slot.getViewIndex()].has_value());
    }

    SceneHeld& NullRenderer::heldAt(const SceneSlot slot)
    {
        assert(holds(slot) && "a view scene that was never made or was dropped");
        return slot.isWorld() ? mWorld : *mViews[slot.getViewIndex()];
    }

    const SceneHeld& NullRenderer::heldAt(const SceneSlot slot) const
    {
        assert(holds(slot) && "a view scene that was never made or was dropped");
        return slot.isWorld() ? mWorld : *mViews[slot.getViewIndex()];
    }

    void NullRenderer::setScene(const SceneSlot slot, const SceneDesc& scene, std::span<const TextureData>)
    {
        heldAt(slot)
            = SceneHeld{ .mIdentity = scene.getIdentity(), .mStructureRevision = scene.getStructureRevision() };
        mOpen = mOpen || slot.isWorld();
    }

    void NullRenderer::extendScene(const SceneSlot slot, const SceneDesc& scene, std::span<const TextureData>)
    {
        heldAt(slot).mStructureRevision = scene.getStructureRevision();
        mOpen = mOpen || slot.isWorld();
    }

    void NullRenderer::placeScene(const SceneSlot slot, [[maybe_unused]] const SceneDesc& scene)
    {
        assert(heldAt(slot).mIdentity == scene.getIdentity() && "a scene placed that the slot was not built from");
        mOpen = mOpen || slot.isWorld();
    }

    SceneHeld NullRenderer::describeHeld(const SceneSlot slot) const
    {
        return heldAt(slot);
    }

    SceneSlot NullRenderer::addViewScene()
    {
        if (mFreeViews.empty())
        {
            mViews.emplace_back(SceneHeld{});
            return SceneSlot::view(static_cast<std::uint32_t>(mViews.size() - 1));
        }
        const std::uint32_t index = mFreeViews.back();
        mFreeViews.pop_back();
        mViews[index] = SceneHeld{};
        return SceneSlot::view(index);
    }

    void NullRenderer::dropViewScene(const SceneSlot slot)
    {
        assert(!slot.isWorld() && holds(slot) && "a view scene dropped that is not held");
        mViews[slot.getViewIndex()].reset();
        mFreeViews.push_back(slot.getViewIndex());
    }

    NullRenderer::GuiTexture& NullRenderer::textureAt(const GuiSlot slot)
    {
        assert(!slot.isNone() && slot.get() < mGuiTextures.size() && mGuiTextures[slot.get()].mHeld);
        return mGuiTextures[slot.get()];
    }

    bool NullRenderer::traceGuiTexture(const GuiSlot texture, [[maybe_unused]] const SceneSlot scene,
        const Viewpoint& view, const GuiTraceOptions& options)
    {
        assert(holds(scene) && "a picture of a scene that is not held");
        GuiTexture& traced = textureAt(texture);
        if (view.mEyes.mWorld.mWidth == 0 || view.mEyes.mWorld.mHeight == 0)
            return false;

        traced.mCopied = traced.mCopied || options.mReadBack;
        return true;
    }

    bool NullRenderer::takeGuiCopy(const GuiSlot texture, const std::span<std::uint8_t> into)
    {
        const GuiTexture& copied = textureAt(texture);
        if (!copied.mCopied)
            return false;

        const std::size_t bytes = std::size_t{ copied.mWidth } * copied.mHeight * 4;
        std::fill_n(into.begin(), std::min(bytes, into.size()), std::uint8_t{ 0 });
        return true;
    }

    GuiSlot NullRenderer::addGuiTexture(const std::uint32_t width, const std::uint32_t height)
    {
        const GuiTexture made{ .mWidth = width, .mHeight = height, .mHeld = true, .mCopied = false };
        if (mFreeGuiTextures.empty())
        {
            mGuiTextures.push_back(made);
            return GuiSlot::at(static_cast<std::uint32_t>(mGuiTextures.size() - 1));
        }
        const std::uint32_t index = mFreeGuiTextures.back();
        mFreeGuiTextures.pop_back();
        mGuiTextures[index] = made;
        return GuiSlot::at(index);
    }

    std::span<std::uint8_t> NullRenderer::lendGuiTexture(const GuiSlot texture, const GuiRegion& region)
    {
        [[maybe_unused]] const GuiTexture& lent = textureAt(texture);
        assert(!mLent.has_value() && "a second rectangle lent before the first was sent");
        assert(region.mX + region.mWidth <= lent.mWidth && region.mY + region.mHeight <= lent.mHeight
            && "a rectangle outside its texture");
        mLent = texture;

        const std::size_t bytes = std::size_t{ region.mWidth } * region.mHeight * 4;
        if (mLending.size() < bytes)
            mLending.resize(bytes);
        return std::span<std::uint8_t>(mLending.data(), bytes);
    }

    void NullRenderer::sendGuiTexture([[maybe_unused]] const GuiSlot texture)
    {
        assert(mLent == texture && "a texture sent that was not lent");
        mLent.reset();
    }

    void NullRenderer::dropGuiTexture(const GuiSlot texture)
    {
        textureAt(texture) = GuiTexture{};
        mFreeGuiTextures.push_back(texture.get());
    }

    void NullRenderer::resize(const std::uint32_t width, const std::uint32_t height)
    {
        mExtents = extentsFor(width, height, mProfile.mUpscale);
    }

    void NullRenderer::setUpscale(const Upscale upscale)
    {
        mProfile.mUpscale = upscale;
        mExtents = extentsFor(mExtents.mOutputWidth, mExtents.mOutputHeight, upscale);
    }

    void NullRenderer::setIndirectLighting(const bool traced)
    {
        mProfile.mReconstruction.mSampling.mIndirect = traced;
    }

    void NullRenderer::setBounceRays(const std::uint32_t rays)
    {
        assert(rays >= 1 && rays <= Shaders::MAX_BOUNCE_RAYS);
        mProfile.mReconstruction.mSampling.mBounceRays = rays;
    }

    FrameTraced NullRenderer::renderFrame(const FrameRequest& request)
    {
        assert(mWorld.mIdentity != 0 && "a frame traced before the world's scene was set");
        // The host collects a frame before it places the next where the ring is full
        // (`collectFrame`), so a frame never finds the ring full here.
        assert(mUnreportedCount < sFramesInFlight && "a frame traced into a full ring");
        ++mUnreportedCount;
        mUnreported[mUnreportedCount - 1] = Unreported{ .mFrame = mFrames++,
            .mInFlight = mUnreportedCount,
            .mReconstruction = mProfile.reconstructionFor(request.mOptions.mReconstruction, mExtents) };
        mOpen = false;
        return {};
    }

    void NullRenderer::skipFrame()
    {
        if (!mOpen)
            return;
        ++mFrames;
        mOpen = false;
    }

    FrameResult NullRenderer::resultOf(const Unreported& frame) const
    {
        FrameResult result;
        result.mFrame = frame.mFrame;
        result.mInFlight = frame.mInFlight;
        result.mReconstruction = frame.mReconstruction;
        return result;
    }

    std::optional<FrameResult> NullRenderer::finishFrame()
    {
        if (mUnreportedCount == 0)
            return std::nullopt;

        const FrameResult result = resultOf(mUnreported[0]);
        std::shift_left(mUnreported.begin(), mUnreported.end(), 1);
        --mUnreportedCount;
        return result;
    }

    std::optional<FrameResult> NullRenderer::collectFrame()
    {
        if (mUnreportedCount < sFramesInFlight)
            return std::nullopt;
        return finishFrame();
    }

    std::string NullRenderer::describeDevice() const
    {
        return "no device: the null renderer, which keeps every promise and draws nothing";
    }

    void NullRenderer::readPixels(std::vector<std::uint8_t>& pixels)
    {
        pixels.assign(std::size_t{ mExtents.mOutputWidth } * mExtents.mOutputHeight * 4, 0);
    }

    void NullRenderer::readDeepPixels(std::vector<std::uint16_t>& samples)
    {
        samples.assign(std::size_t{ mExtents.mOutputWidth } * mExtents.mOutputHeight * 4, 0);
    }
}
