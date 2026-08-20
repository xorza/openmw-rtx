#include "shot.hpp"

#include <chrono>
#include <cstring>
#include <ostream>
#include <vector>

#include <osg/Image>
#include <osgDB/WriteFile>

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
#include <components/rtx/scenedesc.hpp>
#include <components/rtx/visibilitypass.hpp>

namespace RtxTool
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        double millisecondsSince(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }

        /// Where the camera ended up once the request's blanks were filled in.
        struct Placement
        {
            osg::Vec3f mOrigin;
            osg::Vec3f mTarget;
        };

        /// Fills in whichever of origin and target the request left out, with a view of the whole
        /// scene from outside it.
        ///
        /// The one placement that needs nothing known about the cell. It is a poor view of an
        /// interior, whose walls are between the camera and everything worth seeing — pass
        /// coordinates for those.
        Placement placeCamera(const osg::BoundingBoxf& bounds, const ShotRequest& request)
        {
            const osg::Vec3f centre = bounds.center();

            osg::Vec3f direction(0.6f, 0.6f, 0.35f);
            direction.normalize();

            // Far enough back that the bounding sphere fits the vertical field of view, and a little
            // further so it is not touching the edges.
            const float distance
                = bounds.radius() / std::tan(osg::DegreesToRadians(request.mFieldOfView) * 0.5f) * 1.15f;

            return Placement{
                .mOrigin = request.mOrigin.value_or(centre + direction * distance),
                .mTarget = request.mTarget.value_or(centre),
            };
        }

        void writePng(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
            const std::vector<std::uint8_t>& pixels)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(static_cast<int>(width), static_cast<int>(height), 1, GL_RGBA, GL_UNSIGNED_BYTE);

            // The compute pass writes row zero at the top; OSG's images start at the bottom.
            const std::size_t stride = std::size_t{ width } * 4;
            for (std::uint32_t y = 0; y < height; ++y)
                std::memcpy(image->data(0, static_cast<int>(height - 1 - y)), pixels.data() + y * stride, stride);

            if (!osgDB::writeImageFile(*image, Files::pathToUnicodeString(path)))
                throw Rtx::Error("cannot write " + Files::pathToUnicodeString(path));
        }
    }

    int renderShot(const Rtx::SceneDesc& scene, const Rtx::InstanceOptions& instanceOptions, const ShotRequest& request)
    {
        std::ostream& out = Debug::getRawStdout();

        if (scene.getInstances().empty())
        {
            out << "Nothing to render: the cell placed no geometry.\n";
            return 1;
        }

        // Walks every mesh, so it is asked for once and the answer kept.
        const osg::BoundingBoxf bounds = scene.getBounds();
        const Placement placement = placeCamera(bounds, request);

        const Clock::time_point deviceStart = Clock::now();
        const Rtx::Instance instance(instanceOptions);
        Rtx::PhysicalDevice physicalDevice = Rtx::PhysicalDevice::select(instance.getHandle());
        const Rtx::Device device(instance, std::move(physicalDevice));
        Rtx::CommandPool pool(device);
        const double deviceMs = millisecondsSince(deviceStart);

        const Clock::time_point buildStart = Clock::now();
        const Rtx::SceneAcceleration acceleration(device, pool, scene);
        const double buildMs = millisecondsSince(buildStart);

        const Rtx::VisibilityPass pass(device, request.mShaderDirectory);

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
        const Rtx::Shaders::VisibilityConstants camera = Rtx::makeCamera(
            placement.mOrigin, placement.mTarget, request.mFieldOfView, request.mWidth, request.mHeight, far);

        const Clock::time_point traceStart = Clock::now();
        pool.submitAndWait([&](VkCommandBuffer commands) {
            target.transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

            pass.record(commands, acceleration.getTopLevel(), target, hitCount, camera);
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
            << "instances:  " << acceleration.getInstanceCount() << '\n'
            << "structures: " << acceleration.getStructureBytes() / 1024 << " KiB\n"
            << "device up:  " << deviceMs << " ms\n"
            << "build:      " << buildMs << " ms\n"
            << "trace:      " << traceMs << " ms (one submit, including the wait)\n";

        return 0;
    }
}
