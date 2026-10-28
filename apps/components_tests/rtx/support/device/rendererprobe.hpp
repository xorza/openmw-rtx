#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/picturerenderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/frameworld.hpp>

namespace Rtx
{
    class Device;
    struct FogNoise;
    class Image;
    struct SeaState;
    struct TracePasses;
    class VulkanRenderer;
}

namespace Rtx::Testing
{
    /// What a test asks of `VulkanRenderer` and a game never does, through the friendship the
    /// renderer grants it, so none of it ships. None is on a frame path: each that reads submits a
    /// copy and waits for it.
    class RendererProbe
    {
    public:
        explicit RendererProbe(VulkanRenderer& renderer)
            : mRenderer(renderer)
        {
        }

        /// The device the renderer made, for a test that holds its memory to a budget of its own.
        const Device& getDevice() const;

        VkExtent2D getPictureRoom() const;

        /// The passes every trace runs, for a test that stands a chain up beside the frame's on
        /// pipelines already made.
        const TracePasses& getTracePasses() const;

        /// How long `slot`'s texture array is, holes included: nought where the slot holds nothing.
        std::uint32_t getTextureCount(SceneSlot slot) const;

        /// The sea every scene is traced with, `SeaState{}` until told. Uploads a spectrum, which
        /// a frame in flight does not feel: the one it replaces goes to the graveyard
        /// (`WavePass::describe`).
        void setSea(const SeaState& sea);

        /// The fog's field every trace reads, `FogNoise::shared`'s until told: for a test that states
        /// where the air is banked. Drains the frames in flight first, since each reads the field.
        void setFogField(const FogNoise& noise);

        /// Copies one of the last frame's g-buffer channels into `values`, tightly packed, widened
        /// to floats whatever the channel holds. The frame's, never a view scene's.
        void readChannel(Channel channel, std::vector<float>& values);

        /// The same for the composite's own output, which no channel holds: the frame a measurement
        /// is taken on, where `readPixels` gives the one a display would show. The trace's answer,
        /// whatever upscales it, and so without the puffs the display lays over it.
        void readComposite(std::vector<float>& values);

        /// The frame the curve last mapped, in linear radiance: the composite with the puffs over it,
        /// for a frame nothing upscales.
        void readShownRadiance(std::vector<float>& values);

        /// What a present would show, the interface over the picture, four bytes a pixel, tightly
        /// packed: where `readPixels` gives the picture alone.
        void readShown(std::vector<std::uint8_t>& pixels);

        /// The whole of a GUI texture as the device holds it, four bytes a pixel, tightly packed,
        /// row zero first.
        void readGuiTexture(GuiSlot texture, std::vector<std::uint8_t>& pixels);

        /// Moves whatever the API has complained about since the last call into `errors`, so that
        /// clearing before a test and reading after it are the same call.
        void takeValidationErrors(std::vector<std::string>& errors);

        /// `VulkanRenderer::traceFrame`: a frame traced from a block written whole.
        void traceFrame(const Shaders::VisibilityConstants& constants, const FrameOptions& options,
            const WorldOptions& described = {});

        /// `VulkanRenderer::tracePicture`: a picture inside the interface from a block written whole.
        bool tracePicture(GuiSlot texture, SceneSlot scene, const Shaders::VisibilityConstants& camera,
            const GuiTraceOptions& options);

    private:
        /// `image`, a channel or the composed frame, read back in the frame's order at its width:
        /// each pixel from where its field keeps it where `fielded`, and from its own place otherwise.
        void readFramed(const Image& image, bool fielded, std::vector<float>& values) const;

        VulkanRenderer& mRenderer;
    };
}
