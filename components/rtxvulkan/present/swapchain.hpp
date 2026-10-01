#pragma once

#include <cstdint>
#include <vector>

#include <volk.h>

#include <components/rtxvulkan/device/owned.hpp>
#include <components/sdlutil/vsyncmode.hpp>

namespace Rtx
{
    class Device;
    class Surface;

    /// The images the window presents, and the two calls that hand them back and forth. The
    /// renderer renders into an image of its own and blits, because the format a surface offers is
    /// not one a compute shader may store to.
    class Swapchain
    {
    public:
        /// @param surface what the images are presented to, which outlives this.
        Swapchain(const Device& device, const Surface& surface, VkExtent2D extent, SDLUtil::VSyncMode verticalSync);

        /// Takes the next image. False means the swapchain no longer matches the window and must be
        /// recreated — which a resize, a monitor change or a compositor restart all cause, and none
        /// of which is an error.
        bool acquire(VkSemaphore ready, std::uint32_t& index);

        /// Hands the image back. False means the same thing as it does for `acquire`.
        /// @param presented signalled when the presentation engine has finished with the image, or
        ///        null where the device offers no such fence. `Presenter::mPresented` says why one
        ///        is wanted.
        bool present(VkSemaphore finished, std::uint32_t index, VkFence presented);

        /// Rebuilds at a new size. The caller must have waited for every frame still in flight.
        void recreate(VkExtent2D extent);

        /// Whether the surface has no extent at all, which is what a minimised window reports.
        /// Asked of the surface and not of this, because what this holds is the size it was last
        /// built at: a window minimised after that still reports its old extent here and none there.
        bool surfaceIsHidden() const;

        /// Says how the presented image should meet the refresh, and answers whether that changed
        /// the present mode, which is what says a rebuild is owed. A setting and not a mode,
        /// because two settings collapse onto one mode where a driver is missing the other.
        bool setVerticalSync(SDLUtil::VSyncMode mode);

        VkExtent2D getExtent() const { return mExtent; }
        VkImage getImage(std::uint32_t index) const { return mImages[index]; }
        std::uint32_t getImageCount() const { return static_cast<std::uint32_t>(mImages.size()); }

    private:
        void create(VkExtent2D extent);
        void destroy();

        const Device& mDevice;
        const Surface& mSurface;
        /// Immediate, for the reason `Immediate` gives: remade only with the device idle, and its
        /// surface goes before the device's graveyard does.
        Immediate<VkSwapchainKHR, vkDestroySwapchainKHR> mHandle;
        VkSurfaceFormatKHR mFormat{};
        VkPresentModeKHR mPresentMode = VK_PRESENT_MODE_FIFO_KHR;

        SDLUtil::VSyncMode mVerticalSync;
        VkExtent2D mExtent{};
        std::vector<VkImage> mImages;
    };
}
