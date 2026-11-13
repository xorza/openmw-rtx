#include "rendererprobe.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include <components/rtx/environment/wavespectrum.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/validation.hpp>
#include <components/rtxvulkan/frame/gbuffer.hpp>
#include <components/rtxvulkan/scene/devicescene.hpp>
#include <components/rtxvulkan/shaders/shared/fields.h>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx::Testing
{
    const Device& RendererProbe::getDevice() const
    {
        return mRenderer.mDevice;
    }

    VkExtent2D RendererProbe::getPictureRoom() const
    {
        return mRenderer.mPictureRoom;
    }

    void RendererProbe::setReservedUpscales(std::vector<Upscale> modes)
    {
        mRenderer.mReservedUpscales = std::move(modes);
        const VkExtent2D output = mRenderer.mTarget.getExtent();
        mRenderer.createTargets(output.width, output.height);
    }

    const TracePasses& RendererProbe::getTracePasses() const
    {
        return mRenderer.mTracePasses;
    }

    const Upscaler& RendererProbe::getUpscaler() const
    {
        return mRenderer.mUpscaler;
    }

    const TraceChain& RendererProbe::getChain() const
    {
        return mRenderer.mFrame.getChain();
    }

    VkDeviceSize RendererProbe::getLooseStructureBytes(const SceneSlot slot) const
    {
        const DeviceScene* held = mRenderer.mScenes.find(slot);
        return held != nullptr ? held->getAcceleration().getBottomLevel().getLooseBytes() : 0;
    }

    std::uint32_t RendererProbe::getTextureCount(const SceneSlot slot) const
    {
        const DeviceScene* held = mRenderer.mScenes.find(slot);
        return held != nullptr ? held->getTextureCount() : 0;
    }

    void RendererProbe::setSea(const SeaState& sea)
    {
        if (sea == mRenderer.mMedia.getSea())
            return;

        mRenderer.mMedia.describeSea(sea);
    }

    void RendererProbe::setFogField(const FogField noise)
    {
        mRenderer.drain();
        mRenderer.mMedia.describeFog(mRenderer.mDevice, noise);
    }

    void RendererProbe::readChannel(const Channel channel, std::vector<float>& values)
    {
        assert(mRenderer.mFrame.getChain().isBuilt());

        // One lookup and not a switch of eleven arms. A channel is its binding, and the buffer
        // is indexed by it.
        readFramed(mRenderer.mFrame.getChain().getChannels().get(channel), storedInFields(channel), values);
    }

    void RendererProbe::readLampLists(std::vector<std::uint32_t>& words)
    {
        assert(mRenderer.mFrame.getChain().isBuilt());

        const Buffer& lists = mRenderer.mFrame.getChain().getChannels().getLampListsOut();
        const Buffer landing
            = Buffer::readBack(mRenderer.mDevice, lists.getSize(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, "lamp lists read");
        mRenderer.mDevice.getPool().submitAndWait([&](VkCommandBuffer commands) {
            handOver(commands, Use::sBufferComputeWrite, Use::sBufferCopyRead);
            lists.copyTo(commands, landing, lists.getSize());
            landing.orderForHostRead(commands);
        });

        words.resize(lists.getSize() / sizeof(std::uint32_t));
        std::memcpy(words.data(), landing.map(), words.size() * sizeof(std::uint32_t));
    }

    void RendererProbe::readComposite(std::vector<float>& values)
    {
        assert(mRenderer.mFrame.getChain().isBuilt());
        readFramed(mRenderer.mFrame.getChain().getColour(), false, values);
    }

    void RendererProbe::readShownRadiance(std::vector<float>& values)
    {
        assert(mRenderer.mShownRadiance != nullptr && "the shown frame read back before any frame was drawn");
        assert(!mRenderer.upscaling()
            && "the upscaler's image read back, which is made to be read by nothing but the display");
        readFramed(*mRenderer.mShownRadiance, false, values);
    }

    void RendererProbe::readShown(std::vector<std::uint8_t>& pixels)
    {
        assert(mRenderer.mTarget.isOpen());

        // An interface the present would have recorded, drawn now: what is shown is what is read.
        if (mRenderer.mGui.isPrepared())
            mRenderer.mGui.submit(mRenderer.mTarget.getPicture(), mRenderer.mTarget.getShown());

        mRenderer.mTarget.getShown().read(VK_IMAGE_LAYOUT_GENERAL, pixels);
    }

    void RendererProbe::readGuiTexture(const GuiSlot texture, std::vector<std::uint8_t>& pixels)
    {
        mRenderer.mGui.getTextures().read(texture, pixels);
    }

    void RendererProbe::takeValidationErrors(std::vector<std::string>& errors)
    {
        errors.clear();

        ValidationLog* log = mRenderer.mInstance.getValidationLog();
        if (log == nullptr)
            return;

        log->takeErrorsOnThisThread(errors);
    }

    NotFinite RendererProbe::takeCountedStores()
    {
        return mRenderer.mRing.takeCountedStores();
    }

    void RendererProbe::traceFrame(
        const Shaders::VisibilityConstants& constants, const FrameOptions& options, const WorldOptions& described)
    {
        mRenderer.traceFrame(constants, options, described);
    }

    bool RendererProbe::tracePicture(const GuiSlot texture, const SceneSlot scene,
        const Shaders::VisibilityConstants& camera, const GuiTraceOptions& options)
    {
        return mRenderer.tracePicture(texture, scene, camera, options);
    }

    void RendererProbe::readFramed(const Image& image, const bool fielded, std::vector<float>& values) const
    {
        std::vector<float> stored;
        image.readFloats(VK_IMAGE_LAYOUT_GENERAL, stored);

        // The frame's extent: an image stored in fields is a column wider where it is odd. Its fields
        // lie as the last frame traced left them, which is the camera the next reprojects against.
        const std::optional<std::uint32_t> tracedAt = mRenderer.mDescriber.getTracedFrame();
        assert(tracedAt.has_value() && "a channel read back before any frame was traced");
        const std::uint32_t swap = Shaders::fieldSwapOf(*tracedAt);
        const std::uint32_t width = mRenderer.mFrame.getChain().getWidth();
        const std::uint32_t height = mRenderer.mFrame.getChain().getHeight();
        const std::size_t texels = std::size_t{ image.getWidth() } * image.getHeight();
        assert(stored.size() % texels == 0);
        const std::size_t components = stored.size() / texels;

        values.resize(std::size_t{ width } * height * components);
        for (std::uint32_t y = 0; y < height; ++y)
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const Shaders::uvec2 at
                    = fielded ? Shaders::storedOf(Shaders::uvec2(x, y), swap, width) : Shaders::uvec2(x, y);
                const std::size_t from = (std::size_t{ at[1] } * image.getWidth() + at[0]) * components;
                const std::size_t to = (std::size_t{ y } * width + x) * components;
                std::copy_n(stored.begin() + static_cast<std::ptrdiff_t>(from), components,
                    values.begin() + static_cast<std::ptrdiff_t>(to));
            }
    }
}
