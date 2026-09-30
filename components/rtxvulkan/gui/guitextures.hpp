#pragma once

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/common/slots.hpp>
#include <components/rtx/renderer/guirenderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/memory/growablebuffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/graphicspipeline.hpp>

namespace Rtx
{
    class Device;

    /// Every texture the GUI draws with, addressed by slot — a font atlas, a skin sheet, a map, a
    /// video frame — nothing like the scene's bindless array. A slot a texture gave back is taken
    /// over before the table grows (`Rtx::SlotPool`). Nothing here waits on the frame path: making
    /// a texture and writing one are recorded into a batch and handed to the pool, to go ahead of
    /// whatever submits next, because every reader needs these copies *ordered* before it, and
    /// waiting for them *finished* would be waiting for the whole traced frame on every frame that
    /// wrote a texture; `finish` is the exception. A texture rests in
    /// `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` between the calls here, which is why the one path
    /// that writes one with device commands goes through `writeWith`.
    class GuiTextures
    {
    public:
        explicit GuiTextures(const Device& device);

        /// Asserts every slot given back, for the reason `~SceneSlots` gives.
        ~GuiTextures();

        /// A slot holding a texture of this size, cleared to nothing.
        GuiSlot add(std::uint32_t width, std::uint32_t height);

        /// Bytes for a rectangle of a texture, to be filled and then handed back with `send` — the
        /// memory the copy will read, so that a video frame does not cross main memory twice. The
        /// rectangle must lie inside the texture, and only one may be lent at a time; both are
        /// asserts. The span is `height` rows of `width` pixels, four bytes each, tightly packed,
        /// row zero first, and stops being writable at `send`. Write it and do not read it back: it
        /// is write-combined memory.
        std::span<std::uint8_t> lend(GuiSlot slot, const GuiRegion& region);

        /// Records the copy of what `lend` handed out. Nothing has run when this returns.
        void send(GuiSlot slot);

        /// What the texture in `slot` holds: straight until a writer through `writeWith` says
        /// otherwise, which is how the pass knows to lay a traced picture down premultiplied.

        AlphaForm alphaOf(GuiSlot slot) const
        {
            assert(holds(slot) && "the form of a slot nothing holds");
            return mForms[slot.get()];
        }

        void drop(GuiSlot slot);

        /// What the pass samples, or null where nothing holds that slot.
        VkImageView getView(GuiSlot slot);

        bool holds(GuiSlot slot) const
        {
            return !slot.isNone() && slot.get() < mImages.size() && !mImages[slot.get()].isEmpty();
        }

        /// Lends the texture in `slot` to a caller that writes it with transfer commands:
        /// `record(image, layout)` is called with it ready to be written and the layout it is in,
        /// and the scope opened around what is recorded is every transfer stage. Ordering *within*
        /// what is recorded stays the caller's.
        ///
        /// @param form what the caller writes, which the slot then holds — `alphaOf`.
        template <class Record>
        void writeWith(GuiSlot slot, AlphaForm form, VkCommandBuffer commands, Record&& record)
        {
            // First, and whatever the caller has already recorded into `commands`: what is pending
            // here writes this image, and left in the batch it would reach the queue after the
            // buffer being recorded rather than before it.
            handOver();

            assert(holds(slot) && "a write to a slot nothing holds");

            const Image& image = mImages[slot.get()];
            mForms[slot.get()] = form;

            image.transition(commands, Use::sFragmentSample, Use::sTransferWrite);

            record(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            image.transition(commands, Use::sTransferWrite, Use::sFragmentSample);
        }

        /// The whole texture in main memory, four bytes a pixel. Costs a transfer off the device.
        void read(GuiSlot slot, std::vector<std::uint8_t>& pixels);

        /// Records a copy of the whole texture into a host-readable buffer kept for the slot, after
        /// whatever `commands` already holds, and stamps it with the timeline value the next
        /// submit signals — the copy rides that submit, in the same batch as the trace that wrote
        /// the texture, so it costs no submit and no wait of its own. `takeCopy` hands the bytes
        /// over once the queue has passed that value. A buffer this replaces is buried, because a
        /// batch recorded against the old one may not have run.
        void readBackWith(GuiSlot slot, VkCommandBuffer commands);

        /// Copies what `readBackWith` left for `slot` into `into`, and answers whether it did:
        /// false until the host has waited past the submit the copy rode, and never a wait, because
        /// the caller asks again next frame. False too where nothing was ever asked of the slot.
        bool takeCopy(GuiSlot slot, std::span<std::uint8_t> into);

        /// Submits what has been recorded, and what was already handed over, and waits for both —
        /// for a resize and shutdown, where there is no next submit.
        void finish();

    private:
        /// Hands what has been recorded to the pool, to go ahead of its next submit. Costs nothing
        /// where nothing is pending, so every accessor can call it.
        void handOver();

        const Device& mDevice;

        std::vector<Image> mImages;

        std::vector<AlphaForm> mForms;

        /// What a trace left for the host, per slot: the buffer, and the timeline value of the
        /// submit that carried the copy into it — the same clock every other resource is stamped
        /// with, and not a count of frames, because the batch rides whatever submits next. Nought
        /// where nothing was asked, which is a value the timeline has always passed and no submit
        /// ever signals.
        struct Copy
        {
            static constexpr std::uint64_t sNever = 0;

            GrowableBuffer mBuffer;
            std::uint64_t mRides = sNever;
        };
        std::vector<Copy> mCopies;

        /// The slots nothing holds. `SlotPool` and not a list of its own, because which free
        /// slot an arrival takes is one rule and this renderer keeps three tables by it.
        SlotPool mFree;

        /// What `lend` handed bytes out of, until `send` records the copy back: a run of the
        /// batch's staging, which the batch gives back stamped with the submit its copies ride.
        GuiSlot mLentSlot;
        GuiRegion mLentRegion;
        StagingRun mLentRun;

        /// Last, so that it is destroyed first: what it has recorded names images that must still
        /// exist when it goes, and its destructor asserts that nothing is recorded — which is what
        /// `VulkanRenderer::~VulkanRenderer` calls `finish` for. A texture given back is kept
        /// on it, because the copy recorded against it may not have run. Its staging is where
        /// `lend` lends from.
        Batch mBatch;
    };
}
