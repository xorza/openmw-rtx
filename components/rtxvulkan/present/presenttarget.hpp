#pragma once

#include <cassert>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/image.hpp>

namespace Rtx
{
    class Device;

    /// The frame at the output extent, as two images: the picture, which the curve writes and
    /// only the next trace rewrites, and what is shown, which the interface draws over a copy of
    /// the picture and a present blits from. Two, because a frame that blends the interface into the
    /// picture leaves no picture without it: a frame with no trace would blend the interface over the
    /// last interface, and a save's thumbnail would hold the menu it was saved from.
    ///
    /// **A present's blit reads what is shown long after the call returned** — it waits the acquire
    /// semaphore — and the next draw writes it: every command buffer opens with a full barrier on
    /// the one queue (`CommandPool::begin`), whose first scope is every command submitted before it,
    /// the blit included. So the next draw's first write waits for the blit on the device, and the
    /// host waits for nothing.
    class PresentTarget
    {
    public:
        /// Where both images rest between their users, said once: the curve leaves the picture
        /// here, the interface takes both from here and leaves them here, a present's blit and a
        /// read back take the shown image and the picture from here and leave them here. The
        /// general layout and any access, because a user meets what the last one, whichever it
        /// was, left behind.
        static constexpr const ImageUse& sResting = Use::sAnyGeneral;

        /// Makes both, black and in `VK_IMAGE_LAYOUT_GENERAL`, because the interface is drawn over
        /// the picture whether or not a frame was traced into it.
        void resize(const Device& device, std::uint32_t width, std::uint32_t height);

        bool isOpen() const { return !mShown.isEmpty(); }

        /// What every frame is presented at — the one statement of the output extent, because
        /// these are the images that carry it. Zero before the first `resize`, which is what an
        /// asked extent is compared against.
        VkExtent2D getExtent() const { return VkExtent2D{ mShown.getWidth(), mShown.getHeight() }; }

        /// The picture without the interface: what the curve writes, a read back reads and the
        /// interface is drawn over.
        Image& getPicture() { return mPicture; }
        const Image& getPicture() const { return mPicture; }

        /// The picture with the interface over it: what a present blits from.
        Image& getShown() { return mShown; }
        const Image& getShown() const { return mShown; }

        /// The picture again at sixteen bits a channel, which the curve writes beside it for a
        /// summed frame alone, without the debug lines: made at the output extent the first time
        /// one asks, in `TonePass::sDeepFormat`, and dropped by `resize`.
        ///
        /// **Made on demand, because only a harness sums**: the frame at the window's extent and
        /// eight bytes a pixel is 133 megabytes at 7680 by 2160, which no player's frame
        /// reads.
        Image& requireDeep(const Device& device);

        /// What `requireDeep` made, which it has since the last `resize`.
        const Image& getDeep() const
        {
            assert(!mDeep.isEmpty() && "a sixteen-bit picture nothing made");
            return mDeep;
        }

    private:
        Image mPicture;
        Image mShown;
        Image mDeep;
    };
}
