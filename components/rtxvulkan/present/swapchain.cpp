#include "swapchain.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/result.hpp>

#include "presentfence.hpp"
#include "surface.hpp"

namespace Rtx
{
    namespace
    {
        bool hasNoExtent(const VkExtent2D extent)
        {
            return extent.width == 0 || extent.height == 0;
        }

        VkSurfaceFormatKHR chooseFormat(const Surface& surface, VkPhysicalDevice device)
        {
            const std::vector<VkSurfaceFormatKHR> formats = surface.getFormats(device);

            if (formats.empty())
                throw Unsupported("the surface offers no formats");

            // A plain unsigned-normalised format, because the blit that fills it converts between
            // formats and an sRGB target would encode an image that `TonePass` has already encoded:
            // the curve and the transfer function have both run by the time anything reaches here.
            for (const VkSurfaceFormatKHR& format : formats)
                if (format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM)
                    return format;

            Log(Debug::Warning) << "This surface offers no unsigned-normalised format, so the blit that "
                                   "fills it will encode an image that is already display-referred.";
            return formats.front();
        }

        /// What the surface will accept, in the order the caller would rather have. FIFO is the
        /// only mode a surface must support, so it ends every list here and nothing below has to
        /// answer for a driver that offers little else.
        VkPresentModeKHR chooseFrom(
            const std::vector<VkPresentModeKHR>& modes, std::initializer_list<VkPresentModeKHR> wanted)
        {
            for (const VkPresentModeKHR mode : wanted)
                if (std::find(modes.begin(), modes.end(), mode) != modes.end())
                    return mode;

            return VK_PRESENT_MODE_FIFO_KHR;
        }

        /// The present mode a vertical sync setting asks for. `Disabled` is mailbox and not
        /// immediate, because what a player turning vsync off reaches for is latency and not a
        /// torn frame; immediate stands behind it for a surface with no mailbox. `Adaptive` is FIFO
        /// that tears when a frame misses its refresh, as the rasterizer's does through SDL.
        /// `Enabled` is FIFO: a frame that meets the refresh is the promise.
        VkPresentModeKHR presentModeFor(const std::vector<VkPresentModeKHR>& modes, SDLUtil::VSyncMode mode)
        {
            switch (mode)
            {
                case SDLUtil::VSyncMode::Disabled:
                    return chooseFrom(modes, { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR });
                case SDLUtil::VSyncMode::Adaptive:
                    return chooseFrom(modes, { VK_PRESENT_MODE_FIFO_RELAXED_KHR });
                case SDLUtil::VSyncMode::Enabled:
                    break;
            }

            return VK_PRESENT_MODE_FIFO_KHR;
        }

        std::string_view nameOf(VkPresentModeKHR mode)
        {
            switch (mode)
            {
                case VK_PRESENT_MODE_MAILBOX_KHR:
                    return "mailbox";
                case VK_PRESENT_MODE_IMMEDIATE_KHR:
                    return "immediate";
                case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
                    return "fifo relaxed";
                default:
                    return "fifo";
            }
        }
    }

    Swapchain::Swapchain(
        const Device& device, const Surface& surface, VkExtent2D extent, const SDLUtil::VSyncMode verticalSync)
        : mDevice(device)
        , mSurface(surface)
        , mVerticalSync(verticalSync)
    {
        const VkPhysicalDevice physical = device.getPhysicalDevice().getHandle();
        Crash::contract(surface.supports(physical, device.getQueueFamily()),
            "a device chosen for a queue that does not present to its window (`PhysicalDevice::select`)");

        choose();
        if (!create(extent))
            throw DeviceError("the window's surface was lost as its first swapchain was made");

        Log(Debug::Info) << "Swapchain: " << mImages.size() << " images, " << nameOf(mPresentMode);
    }

    bool Swapchain::create(VkExtent2D extent)
    {
        const Crash::NoteScope noted("making the swapchain at {}x{}", extent.width, extent.height);
        const std::optional<VkSurfaceCapabilitiesKHR> asked
            = mSurface.getCapabilities(mDevice.getPhysicalDevice().getHandle());
        if (!asked.has_value())
            return false;
        const VkSurfaceCapabilitiesKHR& capabilities = *asked;

        // A compositor that has already decided the size says so here; otherwise the window's size
        // is the request, clamped to what the surface will accept.
        if (capabilities.currentExtent.width != UINT32_MAX)
            mExtent = capabilities.currentExtent;
        else
            mExtent = VkExtent2D{
                std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
            };

        if (hasNoExtent(mExtent))
        {
            mExtent = VkExtent2D{};
            return true;
        }

        std::uint32_t images = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0)
            images = std::min(images, capabilities.maxImageCount);

        // What the surface will take, asked rather than assumed. The frame reaches the screen
        // as a blit, so a surface that will not be a transfer destination cannot be presented to at
        // all — and this renderer has no second way of filling one.
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
            throw Unsupported("this surface will not take a transfer, and the frame reaches it as a blit");

        // Opaque, and refused rather than substituted: the other modes blend a frame whose alpha
        // this renderer never set against whatever stands behind the window.
        if ((capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0)
            throw Unsupported("this surface offers no opaque composite alpha, and the frame carries no alpha to blend");

        const VkSwapchainCreateInfoKHR create{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = mSurface.getHandle(),
            .minImageCount = images,
            .imageFormat = mFormat.format,
            .imageColorSpace = mFormat.colorSpace,
            .imageExtent = mExtent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = capabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = mPresentMode,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE,
        };
        VkSwapchainKHR made = VK_NULL_HANDLE;
        const VkResult result = vkCreateSwapchainKHR(mDevice.getHandle(), &create, nullptr, &made);
        if (result == VK_ERROR_SURFACE_LOST_KHR)
            return false;
        checkVk(result, "vkCreateSwapchainKHR");
        mHandle = Immediate<VkSwapchainKHR, vkDestroySwapchainKHR>(mDevice.getHandle(), made);
        mPresents = 0;
        mAwaited = 0;

        mImages = enumerateVk<VkImage>("vkGetSwapchainImagesKHR", [&](std::uint32_t* count, VkImage* into) {
            return vkGetSwapchainImagesKHR(mDevice.getHandle(), mHandle.get(), count, into);
        });
        return true;
    }

    void Swapchain::choose()
    {
        const VkPhysicalDevice physical = mDevice.getPhysicalDevice().getHandle();
        mFormat = chooseFormat(mSurface, physical);
        mPresentModes = mSurface.getPresentModes(physical);
        mPresentMode = presentModeFor(mPresentModes, mVerticalSync);
    }

    void Swapchain::release()
    {
        mHandle.reset();
        mImages.clear();
        mPresents = 0;
        mAwaited = 0;
    }

    bool Swapchain::recreate(VkExtent2D extent)
    {
        release();
        return create(extent);
    }

    bool Swapchain::renew(VkExtent2D extent)
    {
        assert(!isMade() && "a swapchain renewed before the old one was let go");
        choose();
        return create(extent);
    }

    bool Swapchain::setVerticalSync(SDLUtil::VSyncMode mode)
    {
        if (mode == mVerticalSync)
            return false;

        mVerticalSync = mode;

        // What the surface offers decides, so two settings can mean one mode. A driver with no
        // relaxed FIFO answers `Adaptive` with plain FIFO, and rebuilding the swapchain to arrive at
        // the mode it already had is a stall for nothing.
        const VkPresentModeKHR wanted = presentModeFor(mPresentModes, mVerticalSync);
        if (wanted == mPresentMode)
            return false;

        mPresentMode = wanted;
        Log(Debug::Info) << "Swapchain: presenting " << nameOf(mPresentMode);

        return true;
    }

    Acquired Swapchain::acquire(VkSemaphore ready, std::uint32_t& index)
    {
        assert(isMade() && "an image asked of a swapchain a hidden surface left unmade");

        // Bounded, and the bound is no image this frame rather than a failure: a compositor that
        // stops handing images back is indistinguishable from one that is merely slow, and both
        // `VK_TIMEOUT` and `VK_NOT_READY` are success codes of an acquire.
        const VkResult result
            = vkAcquireNextImageKHR(mDevice.getHandle(), mHandle.get(), sPatience, ready, VK_NULL_HANDLE, &index);

        switch (result)
        {
            case VK_SUCCESS:
            // Suboptimal still produces a usable image; taking it and rebuilding after the present
            // keeps the semaphore that was just signalled from being left dangling.
            case VK_SUBOPTIMAL_KHR:
                return Acquired::Image;
            case VK_ERROR_OUT_OF_DATE_KHR:
                return Acquired::Stale;
            case VK_TIMEOUT:
            case VK_NOT_READY:
                return Acquired::NotYet;
            case VK_ERROR_SURFACE_LOST_KHR:
                return Acquired::Lost;
            default:
                checkVk(mDevice, result, "vkAcquireNextImageKHR");
                return Acquired::Image;
        }
    }

    Presented Swapchain::present(VkSemaphore finished, std::uint32_t index, PresentFence* presented)
    {
        const VkFence fence = presented != nullptr ? presented->arm(mDevice) : VK_NULL_HANDLE;
        const VkSwapchainPresentFenceInfoKHR signalled{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR,
            .pNext = nullptr,
            .swapchainCount = 1,
            .pFences = &fence,
        };

        const std::uint64_t id = mPresents + 1;
        const VkPresentIdKHR identified{
            .sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
            .pNext = presented != nullptr ? &signalled : nullptr,
            .swapchainCount = 1,
            .pPresentIds = &id,
        };

        const VkSwapchainKHR presenting = mHandle.get();
        const VkPresentInfoKHR present{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = mDevice.hasPresentWait() ? static_cast<const void*>(&identified) : identified.pNext,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &finished,
            .swapchainCount = 1,
            .pSwapchains = &presenting,
            .pImageIndices = &index,
            .pResults = nullptr,
        };

        const VkResult result = vkQueuePresentKHR(mDevice.getQueue(), &present);
        if (presented != nullptr)
            presented->answered(result);
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
            mPresents = id;
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            return Presented::Stale;
        if (result == VK_ERROR_SURFACE_LOST_KHR)
            return Presented::Lost;

        checkVk(mDevice, result, "vkQueuePresentKHR");
        return Presented::Shown;
    }

    void Swapchain::awaitLatency()
    {
        constexpr std::uint64_t sLatency = 2;
        constexpr std::uint64_t sPatienceNs = 100'000'000;

        const bool refreshed
            = mPresentMode == VK_PRESENT_MODE_FIFO_KHR || mPresentMode == VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        if (!mDevice.hasPresentWait() || !refreshed || !isMade() || mPresents < sLatency)
            return;

        const std::uint64_t awaited = mPresents + 1 - sLatency;
        if (awaited <= mAwaited)
            return;
        mAwaited = awaited;

        // A swapchain the surface outgrew or lost is the next present's to report, which the
        // presenter rebuilds from; a wait that ran out lets the frame go on.
        const VkResult result = vkWaitForPresentKHR(mDevice.getHandle(), mHandle.get(), awaited, sPatienceNs);
        if (result == VK_SUCCESS || result == VK_TIMEOUT || result == VK_SUBOPTIMAL_KHR
            || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR)
            return;
        checkVk(mDevice, result, "vkWaitForPresentKHR");
    }

    SurfaceState Swapchain::getSurfaceState() const
    {
        const std::optional<VkSurfaceCapabilitiesKHR> capabilities
            = mSurface.getCapabilities(mDevice.getPhysicalDevice().getHandle());
        if (!capabilities.has_value())
            return SurfaceState::Lost;
        return hasNoExtent(capabilities->currentExtent) ? SurfaceState::Hidden : SurfaceState::Shown;
    }
}
