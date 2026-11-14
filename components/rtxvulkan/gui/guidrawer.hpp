#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/renderer/guirenderer.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/growablebuffer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>

#include "guipass.hpp"
#include "guitextures.hpp"

namespace Rtx
{
    class Device;
    class Image;

    /// The interface over the frame: its pass, the textures it samples, and a ring of its own beside
    /// the frame's. Taken after the frame is submitted, recorded into the present's submit or one of
    /// its own, and waited for by nobody but the draw two behind it, because a menu is drawn on
    /// frames with no world and the frame has nothing to gain by being held open for it.
    class GuiDrawer
    {
    public:
        explicit GuiDrawer(const Device& device);

        GuiTextures& getTextures() { return mTextures; }
        const GuiTextures& getTextures() const { return mTextures; }

        /// Takes `batches` of `vertices` for the next draw, replacing a draw taken and never
        /// recorded: a frame that was not shown.
        void prepare(std::span<const GuiVertex> vertices, std::span<const GuiBatch> batches);

        /// Whether a draw was taken and not yet recorded.
        bool isPrepared() const { return mPrepared; }

        /// Records the draw taken into `commands`: `picture` into `shown` whole, and the batches
        /// over it. Both are taken from `PresentTarget::sResting`, `shown` rewritten whole; `picture`
        /// is left there and `shown` in `shownAfter`, the use what comes next reads it in.
        void record(VkCommandBuffer commands, const Image& picture, const Image& shown, const ImageUse& shownAfter);

        /// `record`, into a submit of its own and not waited for, where nothing else is about to
        /// submit it.
        void submit(const Image& picture, const Image& shown);

    private:
        /// What one draw records into and draws out of. The vertices carry the submit that bound
        /// them, which is what says the slot is free again.
        struct Slot
        {
            LentCommands mCommands;

            /// Rewritten every draw and grown to the busiest one so far, staged as every host write is
            /// (`Uploads`).
            GrowableBuffer mVertices;
        };

        const Device& mDevice;
        GuiPass mPass;
        GuiTextures mTextures;

        /// The batches, resolved from slots to what the pass wants. Kept so that a draw allocates
        /// nothing.
        std::vector<GuiDraw> mDraws;

        PerSlot<Slot> mSlots;

        /// How many draws were prepared, which picks the slot: the next on every prepare, drawn or not, so a slot
        /// waits on its use two prepares ago. A count of draws recorded held the slot after a prepare no present
        /// showed, and the next prepare then waited on the write it had itself deferred, a device frame a frame
        /// through a resize drag.
        std::uint64_t mPrepares = 0;

        /// The slot the prepared draw is recorded from.
        FrameSlot mSlot{ 0 };

        bool mPrepared = false;
    };
}
