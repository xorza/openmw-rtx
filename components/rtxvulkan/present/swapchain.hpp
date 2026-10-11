#pragma once

#include <cstdint>
#include <vector>

#include <volk.h>

#include <components/rtxvulkan/device/owned.hpp>
#include <components/sdlutil/vsyncmode.hpp>

namespace Rtx
{
    class Device;
    class PresentFence;
    class Surface;

    /// What an acquire came to.
    enum class Acquired
    {
        /// An image, and the semaphore will be signalled.
        Image,

        /// The swapchain no longer matches the window and must be recreated — which a resize, a
        /// monitor change or a compositor restart all cause, and none of which is an error.
        Stale,

        /// No image within the wait, and the semaphore untouched: a compositor that stops handing
        /// images back to a window it does not show holds an acquire for as long as the window stays
        /// hidden. The frame goes unshown and the next one asks again.
        NotYet,

        /// The surface is lost and has to be made again from the window: the display it stood on
        /// went away, or the window system took it back.
        Lost,
    };

    /// What the surface says of the window.
    enum class SurfaceState
    {
        /// It has an extent, and a swapchain can stand on it.
        Shown,

        /// It has no extent, which is what a minimised window reports.
        Hidden,

        /// As `Acquired::Lost`.
        Lost,
    };

    /// What a present came to.
    enum class Presented
    {
        Shown,

        /// As `Acquired::Stale`.
        Stale,

        /// As `Acquired::Lost`.
        Lost,
    };

    /// The images the window presents, and the two calls that hand them back and forth. The
    /// renderer renders into an image of its own and blits, because the format a surface offers is
    /// not one a compute shader may store to.
    class Swapchain
    {
    public:
        /// @param surface what the images are presented to, which outlives this.
        Swapchain(const Device& device, const Surface& surface, VkExtent2D extent, SDLUtil::VSyncMode verticalSync);

        /// Takes the next image into `index` where the answer is `Image`.
        Acquired acquire(VkSemaphore ready, std::uint32_t& index);

        /// Hands the image back.
        /// @param presented signalled when the presentation engine has finished with the image, and
        ///        told whether this present owes it, or null where the device offers no such fence.
        Presented present(VkSemaphore finished, std::uint32_t index, PresentFence* presented);

        /// Rebuilds at a new size, and answers false where the surface is lost and nothing stands.
        /// The caller must have waited for every frame still in flight.
        [[nodiscard]] bool recreate(VkExtent2D extent);

        /// Lets the swapchain go, which a surface made again needs first. The caller must have
        /// waited for every frame still in flight.
        void release();

        /// Makes the swapchain on a surface made again since `release`: the format and the present
        /// mode chosen again, since a new surface can offer others. Answers as `recreate` does.
        [[nodiscard]] bool renew(VkExtent2D extent);

        /// Whether the swapchain stands. **A surface with no extent takes none**: a minimised window
        /// on Win32 reports nought as its every bound, and an `imageExtent` outside them is invalid
        /// usage, so nothing is made until a rebuild finds the window back.
        bool isMade() const { return !mImages.empty(); }

        /// Asked of the surface and not of this, because what this holds is the size it was last
        /// built at: a window minimised after that still reports its old extent here and none there.
        SurfaceState getSurfaceState() const;

        /// Says how the presented image should meet the refresh, and answers whether that changed
        /// the present mode, which is what says a rebuild is owed. A setting and not a mode,
        /// because two settings collapse onto one mode where a driver is missing the other.
        bool setVerticalSync(SDLUtil::VSyncMode mode);

        VkExtent2D getExtent() const { return mExtent; }
        VkImage getImage(std::uint32_t index) const { return mImages[index]; }
        std::uint32_t getImageCount() const { return static_cast<std::uint32_t>(mImages.size()); }

    private:
        /// False where the surface is lost.
        [[nodiscard]] bool create(VkExtent2D extent);

        /// The format and the present mode, from what the surface offers.
        void choose();

        const Device& mDevice;
        const Surface& mSurface;
        /// Immediate, for the reason `Immediate` gives: remade only with the device idle, and its
        /// surface goes before the device's graveyard does.
        Immediate<VkSwapchainKHR, vkDestroySwapchainKHR> mHandle;
        VkSurfaceFormatKHR mFormat{};
        VkPresentModeKHR mPresentMode = VK_PRESENT_MODE_FIFO_KHR;

        /// What the surface offers, asked once a surface, so that `setVerticalSync` asks it
        /// nothing: a setting can change while the surface is lost, which answers no question.
        std::vector<VkPresentModeKHR> mPresentModes;

        SDLUtil::VSyncMode mVerticalSync;
        VkExtent2D mExtent{};
        std::vector<VkImage> mImages;
    };
}
