#include "shot.hpp"

#include "placement.hpp"
#include "png.hpp"

#include <chrono>
#include <ostream>
#include <vector>

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
#include <components/rtx/texture.hpp>
#include <components/rtx/visibilitypass.hpp>
#include <components/rtxbridge/texturebuilder.hpp>

namespace RtxTool
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        double millisecondsSince(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
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

        const Rtx::VisibilityPass pass(device, request.mShaderDirectory, textures.getLayout());
        const Rtx::VisibilityInputs inputs{
            .mScene = acceleration.getTopLevel(),
            .mBuffers = &buffers,
            .mTextures = textures.getSet(),
        };

        Rtx::Image target(device, request.mWidth, request.mHeight, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

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
        camera.mAmbient = request.mAmbient;
        camera.mLightCount = static_cast<std::uint32_t>(scene.getLights().size());

        const Clock::time_point traceStart = Clock::now();
        pool.submitAndWait([&](VkCommandBuffer commands) {
            target.transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

            pass.record(commands, inputs, target, hitCount, camera);
        });
        const double traceMs = millisecondsSince(traceStart);

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
            << "trace:      " << traceMs << " ms (one submit, including the wait)\n";

        return 0;
    }
}
