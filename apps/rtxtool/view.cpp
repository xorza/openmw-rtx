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
#include <components/rtx/camera.hpp>
#include <components/rtx/renderer.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/texturebuilder.hpp>
#include <components/rtxvulkan/buffer.hpp>
#include <components/rtxvulkan/commands.hpp>
#include <components/rtxvulkan/compositepass.hpp>
#include <components/rtxvulkan/device.hpp>
#include <components/rtxvulkan/gbuffer.hpp>
#include <components/rtxvulkan/image.hpp>
#include <components/rtxvulkan/instance.hpp>
#include <components/rtxvulkan/physicaldevice.hpp>
#include <components/rtxvulkan/result.hpp>
#include <components/rtxvulkan/sceneacceleration.hpp>
#include <components/rtxvulkan/scenebuffers.hpp>
#include <components/rtxvulkan/swapchain.hpp>
#include <components/rtxvulkan/texture.hpp>
#include <components/rtxvulkan/visibilitypass.hpp>

#include "lighting.hpp"
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

        /// Runs its lambda however the scope is left.
        ///
        /// **The frame loop can throw, and what freed its semaphores and fences came after it.** A
        /// `checkVk` failure anywhere in a frame left all seven behind — and with the layers on by
        /// default that is worse than a leak: `vkDestroyDevice` reports them, the abort policy fires
        /// while the stack is still unwinding, and the error that actually happened never reaches
        /// anyone. What the loop broke on is what should be printed.
        template <typename Run>
        class OnScopeExit
        {
        public:
            explicit OnScopeExit(Run run)
                : mRun(std::move(run))
            {
            }

            OnScopeExit(const OnScopeExit&) = delete;
            OnScopeExit& operator=(const OnScopeExit&) = delete;

            ~OnScopeExit() { mRun(); }

        private:
            Run mRun;
        };

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

    int runWindow(const Rtx::SceneDesc& scene, Resource::ImageManager& images, const Rtx::ValidationOptions& validation,
        const ViewRequest& request)
    {
        if (scene.getInstances().empty())
        {
            out() << "Nothing to show: the cell placed no geometry.\n";
            return 1;
        }

        Window window(request.mTitle, request.mWidth, request.mHeight);

        // The window has to exist before the instance, because only it knows which surface
        // extensions the platform wants.
        // Still building its own device: the window path drives a swapchain directly, and moves
        // behind `Renderer` when presentation does. See docs/rtx/backends.md §5.4.
        Rtx::InstanceOptions options = Rtx::toInstanceOptions(validation);
        options.mSurfaceExtensions = window.getInstanceExtensions();

        const Rtx::Instance instance(options);
        Rtx::PhysicalDevice physicalDevice = Rtx::PhysicalDevice::select(instance.getHandle());
        const Rtx::Device device(instance, std::move(physicalDevice), { VK_KHR_SWAPCHAIN_EXTENSION_NAME });

        Rtx::CommandPool pool(device);
        const Rtx::SceneAcceleration acceleration(device, pool, scene);
        const Rtx::SceneBuffers buffers(device, pool, scene, acceleration.getIndices());
        // The bridge decodes and describes; the backend uploads. Held because the descriptions
        // span its storage until the upload has finished.
        const RtxBridge::SceneTextures described(scene, images);
        const Rtx::TextureArray textures(device, pool, described.getDescriptions());

        const Rtx::VisibilityPass pass(device, pool, request.mShaderDirectory, textures.getLayout());
        const Rtx::CompositePass composite(device, request.mShaderDirectory);
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

        // Bound and never touched: a window renders one frame at a time and has no use for a running
        // sum, so it leaves `mAccumulate` at zero and the composite skips this entirely. It exists
        // because the descriptor has to point somewhere, and it is full size because the composite
        // will not take one smaller than the target. One serves every frame in flight — nothing
        // writes it, so there is nothing for them to race over.
        const auto makeHistory = [&device](VkExtent2D extent) {
            return std::make_unique<Rtx::Image>(
                device, extent.width, extent.height, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT);
        };
        std::unique_ptr<Rtx::Image> history = makeHistory(swapchain.getExtent());

        // One set of channels and not one per frame in flight, which the targets beside them are.
        // The difference is that `begin` waits for the last composite to have finished reading
        // before the next trace overwrites them — being in one submit orders a frame against
        // itself and says nothing about the frame still running beside it.
        const auto makeChannels = [&device](VkExtent2D extent) {
            return std::make_unique<Rtx::GBuffer>(device, extent.width, extent.height);
        };
        std::unique_ptr<Rtx::GBuffer> channels = makeChannels(swapchain.getExtent());

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
        //
        // **Rebuilt with the swapchain, because the image count is the surface's to decide.** A
        // recreate can come back with a different number of images, and a vector sized to the old
        // one is then indexed past its end by `rendered[image]` — which hands `vkQueueSubmit2` a
        // semaphore made of whatever was next on the heap. The layers call that out at once; with
        // them off it is a frozen window and nothing in the log.
        std::vector<VkSemaphore> rendered;

        /// What the last frame to use each swapchain image waits on, so it is not used again while
        /// its present is still outstanding.
        ///
        /// **Mailbox hands an image back before the presentation engine has finished with it.** A
        /// queued frame that a newer one replaces is free to be acquired again immediately, and
        /// signalling `rendered[image]` a second time while the first present's wait is still
        /// pending is undefined — the case the frames-in-flight fence alone does not cover, because
        /// it counts frames rather than images.
        std::vector<VkFence> presenting;

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

        const auto remakeImageSync = [&] {
            for (VkSemaphore semaphore : rendered)
                vkDestroySemaphore(device.getHandle(), semaphore, nullptr);

            rendered.assign(swapchain.getImageCount(), VK_NULL_HANDLE);
            for (VkSemaphore& semaphore : rendered)
                semaphore = makeSemaphore(device.getHandle());

            // Nothing is in flight after a `waitIdle`, and nothing at all before the first frame.
            presenting.assign(swapchain.getImageCount(), VK_NULL_HANDLE);
        };

        remakeImageSync();

        // **`vkDeviceWaitIdle` rather than `Device::waitIdle`, because this runs in a destructor.**
        // The wrapper reports a failure by throwing, and throwing while the stack is already
        // unwinding is a call to `std::terminate` — which would replace the error being reported
        // with no error at all. Nothing here can be done about a device that will not go idle.
        const OnScopeExit freeFrameSync([&] {
            vkDeviceWaitIdle(device.getHandle());

            for (const VkSemaphore semaphore : rendered)
                vkDestroySemaphore(device.getHandle(), semaphore, nullptr);

            for (std::uint32_t i = 0; i < sFramesInFlight; ++i)
            {
                vkDestroySemaphore(device.getHandle(), acquired[i], nullptr);
                vkDestroyFence(device.getHandle(), finished[i], nullptr);
            }
        });

        const auto rebuild = [&] {
            device.waitIdle();
            swapchain.recreate(window.getExtent());
            targets = makeTargets(swapchain.getExtent());
            history = makeHistory(swapchain.getExtent());
            channels = makeChannels(swapchain.getExtent());
            remakeImageSync();
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

            history->transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

            channels->begin(commands);
            pass.record(commands, inputs, *channels, hitCount, constants);
            channels->handOver(commands);
            composite.record(commands, *channels, *history, target,
                Rtx::Shaders::CompositeConstants{
                    .mWidth = constants.mWidth, .mHeight = constants.mHeight, .mAccumulate = 0 });

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
                        std::vector<std::uint8_t> pixels;
                        last.read(pool, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pixels);
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

            // **This image may still be in the presentation engine's hands.** Mailbox releases a
            // frame the moment a newer one replaces it, so an image can come back round before the
            // present that queued it has consumed `rendered[image]` — and the frames-in-flight
            // fence does not cover that, because two frames in flight over three images is not the
            // same count. Waiting on whatever frame last used *this* image is what closes it.
            if (presenting[image] != VK_NULL_HANDLE)
                Rtx::checkVk(
                    vkWaitForFences(device.getHandle(), 1, &presenting[image], VK_TRUE, UINT64_MAX), "vkWaitForFences");

            presenting[image] = finished[frame];

            Rtx::checkVk(vkResetFences(device.getHandle(), 1, &finished[frame]), "vkResetFences");

            const VkExtent2D extent = swapchain.getExtent();
            Rtx::Shaders::VisibilityConstants constants = Rtx::makeCamera(
                camera.getOrigin(), camera.getTarget(), request.mFieldOfView, extent.width, extent.height, far);
            constants.mShowAlbedo = request.mShowAlbedo ? 1u : 0u;

            // The window is the only path with a clock. A screenshot leaves the sea still, so two
            // runs of one build agree pixel for pixel.
            CellLighting lighting = request.mLighting;
            lighting.mSeconds = static_cast<float>(std::chrono::duration<double>(now - began).count());
            applyLighting(lighting, constants);

            // What the fog's step jitter varies by. A screenshot leaves it at zero and gets the same
            // frame twice; here it has to move, or twenty-four shells stand still in front of the
            // camera and the jitter hides nothing.
            constants.mFrame = drawn;

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
            "\n{} frames in {:.2f} s, {:.0f} fps average", drawn, lasted, drawn / std::max(lasted, 1e-6));

        // The same caveat `shot` prints beside its own figure: the layers are on by default outside
        // a Release build and cost about half the frame rate between them, so this is not a number
        // to compare against anything without `--validation=false`.
        if (instance.getValidationLog() != nullptr)
            out() << ", with the validation layers on";

        out() << '\n';
        printCamera(camera);

        return 0;
    }
}
