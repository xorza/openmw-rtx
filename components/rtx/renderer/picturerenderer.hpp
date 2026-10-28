#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <osg/Vec3f>

#include <components/rtx/frame/camera.hpp>
#include <components/rtx/shaders/visibility.h>

#include "sceneholder.hpp"
#include "slot.hpp"

namespace Rtx
{
    /// The light a picture inside the interface stands in, which no world describes: a flat sun
    /// and an ambient, as the rasterizer lights its previews, in the renderer's irradiance. Drawn
    /// with no sky source's shadow, the doll's and the map's alike, as the rasterizer draws them.
    struct PictureLight
    {
        /// Toward the sun, unit, or nought for none.
        osg::Vec3f mDirection;
        osg::Vec3f mIrradiance;
        osg::Vec3f mAmbient;
    };

    /// What a picture inside the interface is asked for, beyond where its camera stands. How much
    /// of the texture the picture fills, from its top-left corner, is the camera's own extent; the
    /// rest is left at `mClear`, whose alpha below one is a picture over the interface's own. The
    /// inventory doll's window resizes and the texture behind it does not.
    struct GuiTraceOptions
    {
        /// What the rest of the texture holds, red first: transparent black for a picture the GUI
        /// composites over what is behind it.
        std::array<float, 4> mClear{};

        /// Whether to leave a copy of the whole texture where `takeGuiCopy` can hand it to the host,
        /// which is the one time a picture inside the interface comes back to main memory.
        bool mReadBack = false;

        /// Which classes the picture draws, `Shaders::MASK_*`, and whether the lamps light it: a map
        /// tile leaves the actors out, and the rasterizer lights its previews by no lamp.
        std::uint32_t mRayMask = Shaders::MASK_EVERY_CLASS;
        bool mLamps = true;

        PictureLight mLight{};
    };

    /// What traces a held scene into a texture of the interface: the part of `Renderer` a picture
    /// inside the interface consumes (`OffscreenTrace`). A scene holder as well, because a picture
    /// is of a scene the picture's owner hands over.
    class PictureRenderer : public SceneHolder
    {
    public:
        virtual ~PictureRenderer() = default;

        PictureRenderer(const PictureRenderer&) = delete;
        PictureRenderer& operator=(const PictureRenderer&) = delete;

        /// Room for pictures inside the interface up to `width` by `height`, asked as each view that
        /// will trace one is made: kept for the session and counted with the frame's targets, so no
        /// picture's first trace takes memory on its frame, over a budget the textures filled. Grown
        /// where a view is larger than what was asked before, and never shrunk.
        virtual void reservePicture(std::uint32_t width, std::uint32_t height) = 0;

        /// Traces the scene from `view` into a GUI texture rather than into the frame: a map
        /// tile, the inventory doll. Not the frame's chain — nothing upscales or averages and the
        /// exposure is one, because a still has no previous frame. Recorded and not run: the picture
        /// rides the next submit, reads the copy of the scene its last placement wrote, and the next
        /// placement of that scene waits for the frame it rode. Whether it recorded the picture:
        /// false for a view of no pixels, which records nothing.
        ///
        /// @param scene what to trace against: a slot `addViewScene` gave out, or the world's for the
        ///        one the frame is drawn from. A map tile is a picture of the world; a doll is not.
        virtual bool traceGuiTexture(
            GuiSlot texture, SceneSlot scene, const Viewpoint& view, const GuiTraceOptions& options)
            = 0;

        /// The copy the last `traceGuiTexture` with `mReadBack` left of `texture`, four bytes a
        /// pixel, tightly packed, row zero first, into `into` as far as it reaches. False until the
        /// copy arrived, which is two frames on, and never a wait.
        virtual bool takeGuiCopy(GuiSlot texture, std::span<std::uint8_t> into) = 0;

        /// Submits every picture recorded and not yet carried and waits for them, for a harness or a
        /// test standing outside any frame. A game never calls it.
        virtual void finishGuiTraces() = 0;

    protected:
        PictureRenderer() = default;
    };
}
