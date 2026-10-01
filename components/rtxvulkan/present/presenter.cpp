#include "presenter.hpp"

#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <osg/Vec2i>

#include <components/misc/presentation.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/result.hpp>

#include "swapchain.hpp"

namespace Rtx
{
    namespace
    {
        /// The window's size in pixels, which is not the size it was asked for on a scaled display.
        VkExtent2D drawableSize(SDL_Window* window)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSizeInPixels(window, &width, &height);
            return VkExtent2D{ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height) };
        }
    }

    std::vector<const char*> Presenter::getInstanceExtensions()
    {
        Uint32 count = 0;
        const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
        if (names == nullptr)
            throw Unsupported(
                std::string("SDL would not name the instance extensions a surface needs: ") + SDL_GetError());

        return std::vector<const char*>(names, names + count);
    }

    Presenter::Presenter(
        const Device& device, const Instance& instance, SDL_Window* window, const SDLUtil::VSyncMode verticalSync)
        : mDevice(device)
        , mInstance(instance.getHandle())
    {
        try
        {
            if (!SDL_Vulkan_CreateSurface(window, mInstance, nullptr, &mSurface))
                throw Unsupported(std::string("SDL would not make a Vulkan surface: ") + SDL_GetError());

            mSwapchain = std::make_unique<Swapchain>(device, mSurface, drawableSize(window), verticalSync);
            makeImageSync();
        }
        catch (...)
        {
            // A constructor that throws gets no destructor, and the surface is the instance's to
            // free whether or not the swapchain on it was ever built.
            destroy();
            throw;
        }
    }

    Presenter::~Presenter()
    {
        destroy();
    }

    void Presenter::destroy()
    {
        // Through `tearDown`, because this runs from a destructor and from the `catch` that tidies
        // up after a constructor that failed, and throwing out of either is `std::terminate`.
        tearDown("the device would not finish before the presenter was taken apart", [&] { mDevice.waitIdle(); });

        releaseImageSync();

        // After the swapchain, which was made from it.
        mSwapchain.reset();

        if (mSurface != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
        mSurface = VK_NULL_HANDLE;
    }

    void Presenter::releaseImageSync()
    {
        // Waited before the semaphores they guard go. A present holds its wait semaphore until
        // the presentation engine is done, and only these say when that is: the device-idle the
        // caller owes proves the queue is empty and nothing more.
        if (mDevice.hasPresentFences())
            for (const SwapImage& image : mImages)
                awaitVk(mDevice, image.mPresented.get(), "the presentation engine letting go of an image");

        // Given back to the pool, which is what lets the rebuild take a fresh set: one kept here
        // per resize or vsync change would be a few dozen sets over a window's life.
        std::vector<VkCommandBuffer> commands;
        commands.reserve(mImages.size());
        for (const SwapImage& image : mImages)
            commands.push_back(image.mCommands);
        mDevice.getPool().recycle(commands);

        mAcquiring.clear();
        mImages.clear();
    }

    void Presenter::makeImageSync()
    {
        const std::uint32_t images = mSwapchain->getImageCount();

        // Made again rather than reused, because a slot can arrive here signalled with nothing
        // left to wait it: a suboptimal acquire hands back both an image and a signal, and it is the
        // present after it that reports the swapchain stale. Destroying the semaphore is what clears
        // that signal, and `releaseImageSync`, run before this, is where it happens.
        mAcquiring.resize(images);
        for (Acquisition& acquisition : mAcquiring)
            acquisition.mSemaphore = makeSemaphore(mDevice);
        mAcquisition = 0;

        // A blit stamp of nought, which the timeline has passed: no image has been blitted onto
        // yet.
        const std::vector<VkCommandBuffer> commands = mDevice.getPool().allocate(images);
        mImages.resize(images);
        for (std::uint32_t index = 0; index < images; ++index)
        {
            SwapImage& image = mImages[index];
            image.mRendered = makeSemaphore(mDevice);
            image.mBlitOn = 0;
            if (mDevice.hasPresentFences())
                image.mPresented = makeSignalledFence(mDevice);
            image.mCommands = commands[index];
        }
    }

    bool Presenter::wantsResize(const VkExtent2D extent)
    {
        if (!mStale && extent.width == getExtent().width && extent.height == getExtent().height)
            return false;

        // A window that is not on screen is left alone. Its surface reports no extent, a
        // swapchain of none is invalid usage, and rebuilding once a frame against a surface that
        // will not take one is a rebuild a minimised game would pay for as long as it stayed
        // minimised. The staleness stands, so the window coming back rebuilds then.
        if (mSwapchain->surfaceIsHidden())
        {
            mStale = true;
            return false;
        }

        return true;
    }

    void Presenter::rebuild(const VkExtent2D extent)
    {
        remake(extent);
        mStale = false;
    }

    void Presenter::setVerticalSync(SDLUtil::VSyncMode mode)
    {
        // A present mode is a property of the swapchain object. Not `rebuild`, because that
        // clears a staleness a window that changed size meanwhile still owes.
        if (mSwapchain->setVerticalSync(mode))
            remake(getExtent());
    }

    void Presenter::remake(const VkExtent2D extent)
    {
        // The sync released between the idle and the recreate, because the present fences it waits
        // are the only word that the presentation engine is done with the old swapchain's images,
        // and destroying a swapchain with a present still reading one is invalid usage.
        mDevice.waitIdle();
        releaseImageSync();
        mSwapchain->recreate(extent);
        makeImageSync();
    }

    VkExtent2D Presenter::getExtent() const
    {
        return mSwapchain->getExtent();
    }

    void Presenter::present(const Image& frame)
    {
        Acquisition& acquisition = mAcquiring[mAcquisition];
        mAcquisition = (mAcquisition + 1) % static_cast<std::uint32_t>(mAcquiring.size());

        // A slot is free when its blit has run, and not when the call that queued it returned.
        // The blit waits the semaphore the acquire signalled, so until it runs both operations are
        // still pending on that semaphore and it may not be handed to another acquire.
        mDevice.waitFor(acquisition.mBlit, "the blit that last took this acquire semaphore");

        std::uint32_t index = 0;
        if (!mSwapchain->acquire(acquisition.mSemaphore.get(), index))
        {
            mStale = true;
            return;
        }

        // This image may still be in the presentation engine's hands. Mailbox releases a frame
        // the moment a newer one replaces it, so an image can come back round before the present
        // that queued it has consumed its semaphore — the case a count of frames in flight does not
        // cover, because it counts frames rather than images.
        SwapImage& image = mImages[index];
        mDevice.waitFor(image.mBlitOn, "the blit that last wrote this image");

        // And the present itself, which is a different moment: the blit's value says the queue has
        // run the copy, and this says the compositor has let go of what it copied into. Without it
        // the semaphore below is signalled again while a present still waits on it.
        if (mDevice.hasPresentFences())
        {
            const VkFence presented = image.mPresented.get();
            awaitVk(mDevice, presented, "the presentation engine letting go of this image");
            checkVk(vkResetFences(mDevice.getHandle(), 1, &presented), "vkResetFences");
        }

        const VkCommandBuffer commands = image.mCommands;
        mDevice.getPool().begin(commands);

        frame.transition(commands, Use::sAnyGeneralWrite, Use::sBlitRead);

        const VkExtent2D extent = mSwapchain->getExtent();
        const osg::Vec2i frameSize(static_cast<int>(frame.getWidth()), static_cast<int>(frame.getHeight()));
        const Misc::Presentation presentation
            = Misc::present(frameSize, osg::Vec2i(static_cast<int>(extent.width), static_cast<int>(extent.height)));
        const osg::Vec2i& origin = presentation.mShownOrigin;
        const osg::Vec2i& shown = presentation.mShownSize;

        // **Black beside a frame whose aspect is not the window's.** No clear where the frame covers
        // the whole image, because the blit writes every pixel of it.
        const bool letterboxed = shown != presentation.mDrawable;
        const VkImage presented = mSwapchain->getImage(index);
        const ImageUse firstWrite = letterboxed ? Use::sClearWrite : Use::sBlitWrite;
        // The source scope names the stage the acquire semaphore is waited at, or the transition
        // is ordered against nothing and can run before the image is ours — which is what
        // `sUndefined`'s `NONE` says, and why it is not the discard used here. Every transfer
        // stage, because the first write is the clear or the blit.
        Barriers taken(commands);
        taken.add(imageBarrier(presented, 0, 1,
            ImageUse{ VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, 0 }, firstWrite));
        taken.flush();

        if (letterboxed)
        {
            const VkClearColorValue black{};
            const VkImageSubresourceRange whole{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdClearColorImage(commands, presented, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &whole);

            Barriers cleared(commands);
            cleared.add(imageBarrier(presented, 0, 1, Use::sClearWrite, Use::sBlitWrite));
            cleared.flush();
        }

        const VkImageBlit region{
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .srcOffsets = { {}, { frameSize.x(), frameSize.y(), 1 } },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstOffsets = { { origin.x(), origin.y(), 0 }, { origin.x() + shown.x(), origin.y() + shown.y(), 1 } },
        };
        // Nearest where the frame is shown pixel for pixel, which copies it exactly; linear where it
        // is scaled, which a nearest scale turns into uneven rows and columns.
        const VkFilter filter = shown == frameSize ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        vkCmdBlitImage(commands, frame.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, presented,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, filter);

        Barriers handed(commands);
        handed.add(imageBarrier(presented, 0, 1, Use::sBlitWrite, Use::sPresent));

        // Back where the next frame's passes expect to find it.
        handed.add(frame.describeTransition(Use::sBlitRead, Use::sAnyGeneralWrite));
        handed.flush();

        // The pool's submit, so it signals the timeline and carries what was deferred ahead of the
        // blit — and waits the acquire and signals the present beside that.
        const VkSemaphoreSubmitInfo wait{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = acquisition.mSemaphore.get(),
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            .deviceIndex = 0,
        };
        const VkSemaphoreSubmitInfo signal{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = image.mRendered.get(),
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0,
        };
        const std::uint64_t blitted = mDevice.getPool().submit(commands,
            std::span<const VkSemaphoreSubmitInfo>(&wait, 1), std::span<const VkSemaphoreSubmitInfo>(&signal, 1));

        acquisition.mBlit = blitted;
        image.mBlitOn = blitted;

        if (!mSwapchain->present(image.mRendered.get(), index, image.mPresented.get()))
            mStale = true;
    }
}
