#pragma once

#include <cstdint>
#include <span>
#include <type_traits>

#include <components/rtx/frame/frameextents.hpp>

#include "slot.hpp"

namespace Rtx
{
    /// One vertex of the GUI, in MyGUI's own layout: a position already in clip space, a colour
    /// packed a byte a channel, and a texture coordinate. MyGUI fills these by the thousand a frame.
    struct GuiVertex
    {
        float mX;
        float mY;
        float mZ;

        /// Red in the low byte, alpha in the high one — MyGUI's `ColourABGR`.
        std::uint32_t mColour;

        float mU;
        float mV;
    };

    /// A frame of GUI is copied out of the buffer MyGUI filled rather than walked.
    static_assert(sizeof(GuiVertex) == 24, "a GUI vertex is what MyGUI writes, and the buffer is read as its own");
    static_assert(std::is_trivial_v<GuiVertex>);

    /// How a run of GUI reaches what is already on the screen.
    enum class GuiBlend : std::uint32_t
    {
        /// Source alpha over the destination, which is every widget there is.
        Over,

        /// Added to the destination. One layer asks for this — the flash when the player is hit —
        /// and over it the same red reads as a tint on the world rather than light in front of it.
        Additive,
    };

    /// One run of vertices drawn with one texture. A run and not an index range, because MyGUI
    /// hands over triangle lists and no indices.
    struct GuiBatch
    {
        /// A slot from `addGuiTexture`.
        GuiSlot mTexture;
        std::uint32_t mFirstVertex = 0;
        std::uint32_t mVertexCount = 0;
        GuiBlend mBlend = GuiBlend::Over;
    };

    /// Which part of a GUI texture a write covers, with the origin at the top left.
    struct GuiRegion
    {
        std::uint32_t mX = 0;
        std::uint32_t mY = 0;
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
    };

    /// What the interface is drawn on and with: its extent, its textures and its draw. The part of
    /// `Renderer` the GUI's backend consumes, as an interface of its own, so it depends on neither
    /// the scene nor the frame halves; the pictures traced into its textures are the renderer's,
    /// because a picture is of a scene.
    class GuiRenderer
    {
    public:
        virtual ~GuiRenderer() = default;

        GuiRenderer(const GuiRenderer&) = delete;
        GuiRenderer& operator=(const GuiRenderer&) = delete;

        /// What the last `Renderer::resize` settled on: the interface is laid out over the output
        /// extent, and a camera has to be built for the render extent.
        virtual FrameExtents getExtents() const = 0;

        /// A texture the GUI draws with, sized once and written whenever it changes, in a table of
        /// its own because a font atlas outlives every scene.
        virtual GuiSlot addGuiTexture(std::uint32_t width, std::uint32_t height) = 0;

        /// Bytes for a rectangle of a texture, four a pixel, tightly packed, row zero first, to be
        /// filled and handed back with `sendGuiTexture`: MyGUI's `lock` and `unlock`. The rectangle
        /// must lie inside the texture and only one may be lent at a time, both asserts. Write the
        /// span and do not read it back: a backend may lend memory the device reads directly.
        virtual std::span<std::uint8_t> lendGuiTexture(GuiSlot texture, const GuiRegion& region) = 0;

        /// Sends what `lendGuiTexture` handed out. The span stops being writable here.
        virtual void sendGuiTexture(GuiSlot texture) = 0;

        virtual void dropGuiTexture(GuiSlot texture) = 0;

        /// Everything the GUI asked to draw, over the finished picture, after the tone curve because
        /// the GUI's colours are display-referred. Vertices are in clip space with +Y up, as MyGUI
        /// produces them.
        virtual void drawGui(std::span<const GuiVertex> vertices, std::span<const GuiBatch> batches) = 0;

    protected:
        GuiRenderer() = default;
    };
}
