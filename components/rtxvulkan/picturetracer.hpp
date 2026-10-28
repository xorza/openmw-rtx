#pragma once

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/guirenderer.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>
#include <components/rtxvulkan/trace/tracechain.hpp>

namespace Rtx
{
    class Device;
    class DeviceScene;
    class DisplayChain;
    class GuiTextures;
    class TraceMedia;

    /// A picture inside the interface — a map tile, the inventory doll, the race preview — traced
    /// into a GUI texture. Its own chain and not the frame's, because borrowing the frame's images
    /// would mean resizing them away from the frame and back between two of them.
    class PictureTracer
    {
    public:
        /// What a picture is traced through beside its own chain, the renderer's and the frame's
        /// as well, handed at each `trace`.
        struct Parts
        {
            TraceMedia& mMedia;
            DisplayChain& mDisplay;
            GuiTextures& mTextures;
        };

        /// @param passes what the chain traces with, which outlives it.
        /// @param radiance the run's, as the frame's chain has it.
        PictureTracer(const Device& device, const TracePasses& passes, RadianceWidth radiance);

        /// What the chain and the byte image take at `room`, which `resize` makes them at.
        static VkDeviceSize bytesAt(const Device& device, VkExtent2D room, RadianceWidth radiance);

        /// Whether the chain stands at exactly `room`, which `resize` to it keeps.
        bool isAt(VkExtent2D room) const
        {
            return isBuilt() && room.width == mChain.getWidth() && room.height == mChain.getHeight();
        }

        bool isBuilt() const { return mChain.isBuilt(); }

        /// Lets the chain and the byte image go, until the next `resize`.
        void release();

        /// Whether a picture this big fits what is built.
        bool holds(VkExtent2D extent) const { return mChain.holds(extent.width, extent.height); }

        /// Makes the chain and the byte image the texture is copied out of at exactly `room`, and
        /// nothing where they stand at it. A frame target, made again only once the device drained
        /// what read the last (`VulkanRenderer::createTargets`).
        void resize(VkExtent2D room);

        /// Records a picture of `traced` under `camera` into `texture`, which `holds` the camera's
        /// extent, on a batch that rides the next submit and that nobody here waits for: the next
        /// placement of the scene waits for what its tables say, and what the picture writes
        /// nothing else reads. Not counted and not timed, because the hit count and the report are
        /// the frame's.
        void trace(GuiSlot texture, SceneSlot scene, const Shaders::VisibilityConstants& camera,
            const GuiTraceOptions& options, DeviceScene& traced, const RenderProfile& profile, const Parts& parts);

    private:
        const Device& mDevice;

        /// What the chain uses inside one stretch of a picture, laid over each other: its own and not
        /// the frame's, so nothing couples the two chains' order.
        ImageArena mArena;

        TraceChain mChain;

        /// The picture as bytes, which is what the texture is copied out of. Empty until room is
        /// reserved, and made with the chain.
        Image mTarget;

        /// What the picture sums its census and its view of the sun into, which nothing reads: the
        /// hit count, the crossings and the glare fader's counts are the frame's, and a picture
        /// traced beside it must not add to them. Bound because the shaders write both regardless.
        Buffer mCounts;
        Buffer mGlareCounts;
    };
}
