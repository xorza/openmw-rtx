#include "shot.hpp"

#include "lighting.hpp"
#include "placement.hpp"
#include "png.hpp"

#include <algorithm>
#include <chrono>
#include <ostream>
#include <vector>

#include <components/debug/debugging.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/camera.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/texturebuilder.hpp>
#include <components/rtxvulkan/buffer.hpp>
#include <components/rtxvulkan/commands.hpp>
#include <components/rtxvulkan/device.hpp>
#include <components/rtxvulkan/image.hpp>
#include <components/rtxvulkan/instance.hpp>
#include <components/rtxvulkan/physicaldevice.hpp>
#include <components/rtxvulkan/sceneacceleration.hpp>
#include <components/rtxvulkan/scenebuffers.hpp>
#include <components/rtxvulkan/texture.hpp>
#include <components/rtxvulkan/visibilitypass.hpp>

namespace RtxTool
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        double millisecondsSince(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        /// What a run of traces of the same frame came to.
        struct TraceTimes
        {
            double mBest;
            double mMedian;
            double mWorst;
        };

        /// The three figures a run of traces is worth quoting by.
        ///
        /// **The best is the answer and the spread is whether to believe it.** A minimum over enough
        /// runs is the least-contended one and the most repeatable; a median an order away from it
        /// says the machine was doing something else, and the number should not be quoted.
        ///
        /// **By reference, and it sorts in place.** A copy would read better at the call site and
        /// cannot be had: taking one loses the compiler its proof that the loop below pushed at
        /// least once, and `front()` on a vector it can no longer see into is a hard warning. The
        /// alternative is a defensive branch for a case the caller cannot produce.
        TraceTimes summarise(std::vector<double>& times)
        {
            std::sort(times.begin(), times.end());
            return TraceTimes{
                .mBest = times.front(),
                .mMedian = times[times.size() / 2],
                .mWorst = times.back(),
            };
        }
    }

    int renderShot(const Rtx::SceneDesc& scene, Resource::ImageManager& images,
        const Rtx::InstanceOptions& instanceOptions, const ShotRequest& request)
    {
        std::ostream& out = Debug::getRawStdout();

        if (scene.getInstances().empty())
        {
            out << "Nothing to render: the cell placed no geometry.\n";
            return 1;
        }

        // Walks every mesh, so it is asked for once and the answer kept.
        const osg::BoundingBoxf bounds = scene.getBounds();
        const Placement placement = placeCamera(bounds, request.mFieldOfView, request.mOrigin, request.mTarget);

        const Clock::time_point deviceStart = Clock::now();
        const Rtx::Instance instance(instanceOptions);
        Rtx::PhysicalDevice physicalDevice = Rtx::PhysicalDevice::select(instance.getHandle());
        const Rtx::Device device(instance, std::move(physicalDevice));
        Rtx::CommandPool pool(device);
        const double deviceMs = millisecondsSince(deviceStart);

        const Clock::time_point buildStart = Clock::now();
        const Rtx::SceneAcceleration acceleration(device, pool, scene);
        const Rtx::SceneBuffers buffers(device, pool, scene, acceleration.getIndices());
        const Rtx::TextureArray textures = RtxBridge::buildTextures(device, pool, scene, images);
        const double buildMs = millisecondsSince(buildStart);

        const Rtx::VisibilityPass pass(device, pool, request.mShaderDirectory, textures.getLayout());
        const Rtx::VisibilityInputs inputs{
            .mScene = acceleration.getTopLevel(),
            .mBuffers = &buffers,
            .mTextures = textures.getSet(),
        };

        Rtx::Image target(device, request.mWidth, request.mHeight, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

        // Sixteen bytes a pixel, which is 33 MiB at 1080p and the price of a sum that neither rounds
        // nor clips. Made even for a run that is not averaging, and full size: the shader leaves it
        // alone then, but the descriptor still has to point at an image the pass will accept.
        Rtx::Image history(
            device, request.mWidth, request.mHeight, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT);

        const Rtx::Buffer hitCount(device, sizeof(std::uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        *static_cast<std::uint32_t*>(hitCount.map()) = 0;
        hitCount.unmap();

        // Far enough to cross any cell: the largest exterior view in the game is a few tens of
        // thousands of units, and a primary ray that reaches this has left the world.
        const float far = bounds.radius() * 8.0f;
        Rtx::Shaders::VisibilityConstants camera = Rtx::makeCamera(
            placement.mOrigin, placement.mTarget, request.mFieldOfView, request.mWidth, request.mHeight, far);
        camera.mShowAlbedo = request.mShowAlbedo ? 1u : 0u;
        applyLighting(request.mLighting, camera);

        // **Accumulating replaces repeating rather than joining it.** A run of `--accumulate=4` that
        // also honoured the repeat default would quietly average eight frames and report four — and
        // a convergence ladder built on that reads as though the first four samples bought nothing,
        // which is exactly what it looked like. A shot traces at least once either way, because a
        // shot is a picture.
        const bool averaging = request.mAccumulate > 0;
        const std::uint32_t frames = averaging ? request.mAccumulate : std::max(request.mRepeat, 1u);

        std::vector<double> traces;
        traces.reserve(frames);

        // **A `do` and not a `for`, for the reason `summarise` takes its argument by reference.** The
        // bound below is `max(..., 1)` and a `for` over it still leaves the compiler unable to prove
        // the body ran, so `front()` on what it filled becomes a hard warning. Looping this way says
        // it structurally.
        std::uint32_t frame = 0;
        do
        {
            // The count is an atomic sum over the frame, so it has to start each one at nothing or
            // the fraction reported below would be however many frames were traced.
            *static_cast<std::uint32_t*>(hitCount.map()) = 0;
            hitCount.unmap();

            // **The seed moves only when the frames are being averaged.** A timing run wants the
            // same work every trace, so that what the spread shows is the machine and not two
            // frames that happened to sample different geometry.
            camera.mFrame = averaging ? frame : 0;
            camera.mAccumulate = averaging ? frame + 1 : 0;

            const Clock::time_point traceStart = Clock::now();
            pool.submitAndWait([&](VkCommandBuffer commands) {
                target.transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

                // The first frame writes the sum without reading it, so it needs no contents and
                // nothing to wait on. Every frame after reads what the last one left, which is a
                // hazard across submits that the fence orders but does not make visible.
                const bool fresh = frame == 0;
                history.transition(commands, fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_GENERAL,
                    fresh ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    fresh ? 0 : VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

                pass.record(commands, inputs, target, history, hitCount, camera);
            });
            traces.push_back(millisecondsSince(traceStart));
            ++frame;
        } while (frame < frames);

        const TraceTimes trace = summarise(traces);

        const std::vector<std::uint8_t> pixels = target.read(pool, VK_IMAGE_LAYOUT_GENERAL);
        writePng(request.mOutput, request.mWidth, request.mHeight, pixels);

        const std::uint32_t hits = *static_cast<const std::uint32_t*>(hitCount.map());
        hitCount.unmap();

        const double fraction
            = static_cast<double>(hits) / (static_cast<double>(request.mWidth) * request.mHeight) * 100.0;

        out << "wrote " << Files::pathToUnicodeString(request.mOutput) << ' ' << request.mWidth << 'x'
            << request.mHeight << '\n'
            << "camera:     " << placement.mOrigin.x() << ", " << placement.mOrigin.y() << ", " << placement.mOrigin.z()
            << "  looking at " << placement.mTarget.x() << ", " << placement.mTarget.y() << ", "
            << placement.mTarget.z() << '\n'
            << "primary rays that hit: " << fraction << "%\n"
            << "instances:  " << acceleration.getInstanceCount() << ", of which "
            << acceleration.getCutoutInstanceCount() << " are cutouts\n"
            << "structures: " << acceleration.getStructureBytes() / 1024 << " KiB\n"
            << "tables:     " << buffers.getBytes() / 1024 << " KiB\n"
            << "textures:   " << textures.getCount() << " in " << textures.getBytes() / 1024 << " KiB\n"
            << "device up:  " << deviceMs << " ms\n"
            << "build:      " << buildMs << " ms\n"
            << "trace:      " << trace.mBest << " ms";

        if (averaging)
            out << " (best of " << frames << " accumulated; median " << trace.mMedian << ", worst " << trace.mWorst
                << ")";
        else if (frames > 1)
            out << " (best of " << frames << "; median " << trace.mMedian << ", worst " << trace.mWorst << ")";
        else
            out << " (one submit, including the wait)";

        // **Said out loud, because it doubles the number above it.** The layers are on by default
        // outside a Release build, and a figure measured under them is not one to compare against
        // anything: core and synchronization validation cost about 6% here, GPU-assisted another
        // 100%. Anyone quoting a trace time wants `--validation=false`.
        if (instance.getValidationLog() != nullptr)
            out << ", with the validation layers on";

        out << '\n';

        return 0;
    }
}
