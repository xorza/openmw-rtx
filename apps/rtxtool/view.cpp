#include "view.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <ostream>
#include <vector>

#include <SDL.h>

#include <components/debug/debugging.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/buffer.hpp>
#include <components/rtx/commands.hpp>
#include <components/rtx/device.hpp>
#include <components/rtx/error.hpp>
#include <components/rtx/image.hpp>
#include <components/rtx/instance.hpp>
#include <components/rtx/physicaldevice.hpp>
#include <components/rtx/sceneacceleration.hpp>
#include <components/rtx/scenebuffers.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtx/swapchain.hpp>
#include <components/rtx/texture.hpp>
#include <components/rtx/visibilitypass.hpp>
#include <components/rtxbridge/texturebuilder.hpp>

#include "placement.hpp"
#include "png.hpp"
#include "window.hpp"

namespace RtxTool
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        /// Two, so the CPU can prepare one frame while the GPU works on the other. More would add
        /// latency to a tool whose whole job is answering a mouse.
        constexpr std::uint32_t sFramesInFlight = 2;

        std::ostream& out()
        {
            return Debug::getRawStdout();
        }

        VkSemaphore makeSemaphore(VkDevice device)
        {
            const VkSemaphoreCreateInfo create{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            VkSemaphore semaphore = VK_NULL_HANDLE;
            Rtx::checkVk(vkCreateSemaphore(device, &create, nullptr, &semaphore), "vkCreateSemaphore");
            return semaphore;
        }

        VkFence makeSignalledFence(VkDevice device)
        {
            const VkFenceCreateInfo create{
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            };
            VkFence fence = VK_NULL_HANDLE;
            Rtx::checkVk(vkCreateFence(device, &create, nullptr, &fence), "vkCreateFence");
            return fence;
        }

        /// The camera as the two lines `views.cfg` wants, so that flying somewhere and keeping it is
        /// a copy and a paste.
        void printCamera(const FlyCamera& camera)
        {
            const osg::Vec3f at = camera.getOrigin();
            const osg::Vec3f to = camera.getTarget();

            out() << std::format("pos = {:.0f}, {:.0f}, {:.0f}\nlook = {:.0f}, {:.0f}, {:.0f}\n", at.x(), at.y(),
                at.z(), to.x(), to.y(), to.z());
        }

        void printHelp()
        {
            out() << "\n"
                     "  W A S D        move,  Q E or ctrl/space for down and up\n"
                     "  right drag     look\n"
                     "  shift / alt    six times faster / seven times slower\n"
                     "  wheel          change the base speed\n"
                     "  P              print this camera as a block for views.cfg\n"
                     "  F2             write a screenshot\n"
                     "  F1             this list\n"
                     "  Esc            quit\n\n";
        }
    }

    int runWindow(const Rtx::SceneDesc& scene, Resource::ImageManager& images,
        const Rtx::InstanceOptions& instanceOptions, const ViewRequest& request)
    {
        if (scene.getInstances().empty())
        {
            out() << "Nothing to show: the cell placed no geometry.\n";
            return 1;
        }

        Window window(request.mTitle, request.mWidth, request.mHeight);

        // The window has to exist before the instance, because only it knows which surface
        // extensions the platform wants.
        Rtx::InstanceOptions options = instanceOptions;
        options.mSurfaceExtensions = window.getInstanceExtensions();

        const Rtx::Instance instance(options);
        Rtx::PhysicalDevice physicalDevice = Rtx::PhysicalDevice::select(instance.getHandle());
        const Rtx::Device device(instance, std::move(physicalDevice), { VK_KHR_SWAPCHAIN_EXTENSION_NAME });

        Rtx::CommandPool pool(device);
        const Rtx::SceneAcceleration acceleration(device, pool, scene);
        const Rtx::SceneBuffers buffers(device, pool, scene, acceleration.getIndices());
        const Rtx::TextureArray textures = RtxBridge::buildTextures(device, pool, scene, images);

        const Rtx::VisibilityPass pass(device, request.mShaderDirectory, textures.getLayout());
        const Rtx::VisibilityInputs inputs{
            .mScene = acceleration.getTopLevel(),
            .mBuffers = &buffers,
            .mTextures = textures.getSet(),
        };

        // Declared after the instance so it is destroyed before it, which the validation layers are
        // otherwise quick to point out.
        const Surface surface(instance.getHandle(), window);
        Rtx::Swapchain swapchain(device, surface.getHandle(), window.getExtent());

        // One per frame in flight, not one shared. The fence a frame waits on is the one from two
        // frames ago, so the frame in between can still be blitting out of a target that a shared
        // one would already be tracing into.
        const auto makeTargets = [&device](VkExtent2D extent) {
            std::vector<std::unique_ptr<Rtx::Image>> targets;
            for (std::uint32_t i = 0; i < sFramesInFlight; ++i)
                targets.push_back(std::make_unique<Rtx::Image>(device, extent.width, extent.height,
                    VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
            return targets;
        };
        std::vector<std::unique_ptr<Rtx::Image>> targets = makeTargets(swapchain.getExtent());

        // The pass counts its hits into this because a screenshot wants the number. A window does
        // not: reading it would mean waiting for the GPU, and what it would say is already on the
        // screen. It is bound, written to, and ignored.
        const Rtx::Buffer hitCount(device, sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        const std::vector<VkCommandBuffer> commandBuffers = pool.allocate(sFramesInFlight);
        std::array<VkSemaphore, sFramesInFlight> acquired{};
        std::array<VkFence, sFramesInFlight> finished{};
        for (std::uint32_t i = 0; i < sFramesInFlight; ++i)
        {
            acquired[i] = makeSemaphore(device.getHandle());
            finished[i] = makeSignalledFence(device.getHandle());
        }

        // One per swapchain image rather than per frame in flight: a present may still be reading
        // the semaphore a frame signalled, and there is no fence that says when it stopped.
        std::vector<VkSemaphore> rendered(swapchain.getImageCount());
        for (VkSemaphore& semaphore : rendered)
            semaphore = makeSemaphore(device.getHandle());

        const osg::BoundingBoxf bounds = scene.getBounds();
        const Placement start = placeCamera(bounds, request.mFieldOfView, request.mOrigin, request.mTarget);

        FlyCamera camera;
        camera.look(start.mOrigin, start.mTarget);

        const float far = std::max(bounds.radius() * 8.0f, 10000.0f);

        printHelp();

        bool running = true;
        bool looking = false;
        bool resized = false;

        std::uint32_t frame = 0;
        std::uint32_t drawn = 0;

        const auto rebuild = [&] {
            device.waitIdle();
            swapchain.recreate(window.getExtent());
            targets = makeTargets(swapchain.getExtent());
            resized = false;
        };

        /// Trace into our own image, then blit it onto the one the window will show.
        ///
        /// Two images rather than one because a compute shader cannot store to most surface formats,
        /// and because every pass after this one wants a target with more precision than a display
        /// has anyway.
        const auto recordFrame = [&](VkCommandBuffer commands, const Rtx::Image& target, VkImage presented,
                                     VkExtent2D extent, const Rtx::Shaders::VisibilityConstants& constants) {
            const VkCommandBufferBeginInfo begin{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            };
            Rtx::checkVk(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer");

            target.transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

            pass.record(commands, inputs, target, hitCount, constants);

            target.transition(commands, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

            // The source scope must name the stage the acquire semaphore is waited at, or the
            // transition is ordered against nothing and can run before the image is ours.
            // TOP_OF_PIPE as a source scope means exactly that: nothing.
            const VkImageMemoryBarrier2 toDestination{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = presented,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            };
            VkDependencyInfo dependency{
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &toDestination,
            };
            vkCmdPipelineBarrier2(commands, &dependency);

            const VkImageBlit region{
                .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .srcOffsets = { {},
                    { static_cast<std::int32_t>(target.getWidth()), static_cast<std::int32_t>(target.getHeight()),
                        1 } },
                .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .dstOffsets
                = { {}, { static_cast<std::int32_t>(extent.width), static_cast<std::int32_t>(extent.height), 1 } },
            };
            vkCmdBlitImage(commands, target.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, presented,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);

            const VkImageMemoryBarrier2 toPresent{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = presented,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
            };
            dependency.pImageMemoryBarriers = &toPresent;
            vkCmdPipelineBarrier2(commands, &dependency);

            Rtx::checkVk(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
        };

        const auto handle = [&](const SDL_Event& event) {
            switch (event.type)
            {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                        resized = true;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_RIGHT)
                    {
                        looking = true;
                        SDL_SetRelativeMouseMode(SDL_TRUE);
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_RIGHT)
                    {
                        looking = false;
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (looking)
                        camera.turn(-event.motion.xrel * 0.0025f, -event.motion.yrel * 0.0025f);
                    break;
                case SDL_MOUSEWHEEL:
                    camera.scaleSpeed(event.wheel.y > 0 ? 1.3f : 1.0f / 1.3f);
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_ESCAPE)
                        running = false;
                    else if (event.key.keysym.sym == SDLK_F1)
                        printHelp();
                    else if (event.key.keysym.sym == SDLK_p)
                        printCamera(camera);
                    else if (event.key.keysym.sym == SDLK_F2 && drawn > 0)
                    {
                        device.waitIdle();

                        const Rtx::Image& last = *targets[(frame + sFramesInFlight - 1) % sFramesInFlight];
                        const std::filesystem::path file
                            = request.mScreenshotDirectory / ("rtx-" + std::to_string(SDL_GetTicks()) + ".png");
                        const std::vector<std::uint8_t> pixels = last.read(pool, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                        writePng(file, last.getWidth(), last.getHeight(), pixels);
                        out() << "wrote " << Files::pathToUnicodeString(file) << '\n';
                    }
                    break;
                default:
                    break;
            }
        };
        Clock::time_point previous = Clock::now();
        const Clock::time_point began = previous;

        // Five times a second: fast enough that the coordinates keep up with the mouse, slow enough
        // that the compositor is not asked to redraw a title bar every frame.
        constexpr auto titleInterval = std::chrono::milliseconds(200);
        Clock::time_point lastTitle = previous;
        std::uint32_t framesSinceTitle = 0;

        while (running)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event) != 0)
                handle(event);

            const Clock::time_point now = Clock::now();
            const float seconds = std::chrono::duration<float>(now - previous).count();
            previous = now;
            camera.advance(std::min(seconds, 0.1f));

            ++framesSinceTitle;
            if (now - lastTitle >= titleInterval)
            {
                const double elapsed = std::chrono::duration<double>(now - lastTitle).count();
                const osg::Vec3f at = camera.getOrigin();
                const VkExtent2D shown = swapchain.getExtent();

                window.setTitle(std::format("{}  |  {:.0f} fps  |  {}x{}  |  {:.0f}, {:.0f}, {:.0f}  |  {:.0f} u/s",
                    request.mTitle, framesSinceTitle / elapsed, shown.width, shown.height, at.x(), at.y(), at.z(),
                    camera.getSpeed()));

                framesSinceTitle = 0;
                lastTitle = now;
            }

            if (resized)
                rebuild();

            Rtx::checkVk(
                vkWaitForFences(device.getHandle(), 1, &finished[frame], VK_TRUE, UINT64_MAX), "vkWaitForFences");

            std::uint32_t image = 0;
            if (!swapchain.acquire(acquired[frame], image))
            {
                rebuild();
                continue;
            }

            Rtx::checkVk(vkResetFences(device.getHandle(), 1, &finished[frame]), "vkResetFences");

            const VkExtent2D extent = swapchain.getExtent();
            Rtx::Shaders::VisibilityConstants constants = Rtx::makeCamera(
                camera.getOrigin(), camera.getTarget(), request.mFieldOfView, extent.width, extent.height, far);
            constants.mShowAlbedo = request.mShowAlbedo ? 1u : 0u;

            const VkCommandBuffer commands = commandBuffers[frame];
            recordFrame(commands, *targets[frame], swapchain.getImage(image), extent, constants);

            const VkSemaphoreSubmitInfo wait{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = acquired[frame],
                .stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT,
            };
            const VkSemaphoreSubmitInfo signal{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                .semaphore = rendered[image],
                .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            };
            const VkCommandBufferSubmitInfo buffer{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .commandBuffer = commands,
            };
            const VkSubmitInfo2 submit{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .waitSemaphoreInfoCount = 1,
                .pWaitSemaphoreInfos = &wait,
                .commandBufferInfoCount = 1,
                .pCommandBufferInfos = &buffer,
                .signalSemaphoreInfoCount = 1,
                .pSignalSemaphoreInfos = &signal,
            };
            Rtx::checkVk(vkQueueSubmit2(device.getQueue(), 1, &submit, finished[frame]), "vkQueueSubmit2");

            if (!swapchain.present(rendered[image], image))
                resized = true;

            frame = (frame + 1) % sFramesInFlight;

            // Counted unconditionally: the summary at the end reports it whether or not a limit
            // was asked for, and `&&` would have skipped the increment in the interactive case.
            ++drawn;
            if (request.mFrames != 0 && drawn >= request.mFrames)
                running = false;
        }

        // One line at the end rather than one a second throughout: a number per second is noise to
        // someone watching the title bar, and scrollback to someone who ran this with --frames.
        const double lasted = std::chrono::duration<double>(Clock::now() - began).count();
        out() << std::format(
            "\n{} frames in {:.2f} s, {:.0f} fps average\n", drawn, lasted, drawn / std::max(lasted, 1e-6));
        printCamera(camera);

        device.waitIdle();
        for (const VkSemaphore semaphore : rendered)
            vkDestroySemaphore(device.getHandle(), semaphore, nullptr);
        for (std::uint32_t i = 0; i < sFramesInFlight; ++i)
        {
            vkDestroySemaphore(device.getHandle(), acquired[i], nullptr);
            vkDestroyFence(device.getHandle(), finished[i], nullptr);
        }

        return 0;
    }
}
