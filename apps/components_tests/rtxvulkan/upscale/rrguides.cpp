#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>
#include <volk.h>

#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/readback.hpp>
#include <components/rtx/common/halffloat.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/shaders/gbuffer.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/shaders/shared/dlss.h>
#include <components/rtxvulkan/shaders/shared/fields.h>

namespace Rtx
{
    namespace
    {
        using RtxRayReconstructionGuidesTest = Testing::DeviceTest;

        /// One row of six pixels, the frame's own order: the sky, a near-black albedo, a vanilla
        /// surface with no lobe, a lobed one beside it, a dark red albedo above the floor, and a
        /// rough lobe. The third and fourth stand either side of the middle, so their rays lean from
        /// the eye's axis alike.
        constexpr std::uint32_t sWidth = 6;
        constexpr std::uint32_t sHeight = 1;

        /// The frame whose field mapping wrote the channels: an odd one, so the fields are swapped and a
        /// pass that read the channels in the frame's own order would read every pixel's neighbour.
        constexpr std::uint32_t sFrame = 7;

        struct Pixel
        {
            osg::Vec3f mNormal;
            float mDistance;
            osg::Vec2f mMotion;
            osg::Vec3f mAlbedo;
            osg::Vec3f mLobeAlbedo;
            float mRoughness;
        };

        const std::array<Pixel, sWidth> sPixels{ {
            { osg::Vec3f(), 0.0f, osg::Vec2f(1.5f, -2.25f), osg::Vec3f(0.2f, 0.2f, 0.2f), osg::Vec3f(1.0f, 1.0f, 1.0f),
                Shaders::SPECULAR_NO_LOBE },
            { osg::Vec3f(0.0f, 0.0f, 1.0f), 10.0f, osg::Vec2f(), osg::Vec3f(0.001f, 0.001f, 0.001f),
                osg::Vec3f(1.0f, 1.0f, 1.0f), Shaders::SPECULAR_NO_LOBE },
            { osg::Vec3f(0.0f, 0.0f, 1.0f), 10.0f, osg::Vec2f(0.5f, 0.25f), osg::Vec3f(0.4f, 0.3f, 0.2f),
                osg::Vec3f(1.0f, 1.0f, 1.0f), Shaders::SPECULAR_NO_LOBE },
            { osg::Vec3f(0.0f, 1.0f, 0.0f), 20.0f, osg::Vec2f(-3.0f, 4.0f), osg::Vec3f(0.5f, 0.5f, 0.5f),
                osg::Vec3f(0.04f, 0.05f, 0.0625f), 0.25f },
            { osg::Vec3f(1.0f, 0.0f, 0.0f), 30.0f, osg::Vec2f(), osg::Vec3f(0.003f, 0.0f, 0.0f),
                osg::Vec3f(1.0f, 1.0f, 1.0f), Shaders::SPECULAR_NO_LOBE },
            { osg::Vec3f(0.0f, -1.0f, 0.0f), 40.0f, osg::Vec2f(), osg::Vec3f(0.25f, 0.5f, 0.75f),
                osg::Vec3f(0.5f, 0.5f, 0.5f), 1.0f },
        } };

        constexpr std::array sBindings{
            computeBinding(Shaders::RR_GUIDES_BIND_SURFACE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_MOTION, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_ALBEDO, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_SPECULAR_ALBEDO, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_SPECULAR, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_DEPTH, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_MOTION_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_DIFFUSE_ALBEDO, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_SPECULAR_ALBEDO_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_NORMAL_ROUGHNESS, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_CONSTANTS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
        };

        /// Puts `values`, four a texel in the frame's order, at each pixel's place in its field, as the
        /// trace stores a channel: halves for a half-float image, floats for a float one.
        template <class Texel>
        std::vector<Texel> inFields(std::span<const Texel> values, const std::uint32_t perPixel)
        {
            std::vector<Texel> stored(values.size());
            for (std::uint32_t x = 0; x < sWidth; ++x)
            {
                const Shaders::uvec2 at = Shaders::storedOf(Shaders::uvec2(x, 0), Shaders::fieldSwapOf(sFrame), sWidth);
                for (std::uint32_t c = 0; c < perPixel; ++c)
                    stored[at.x() * perPixel + c] = values[x * perPixel + c];
            }
            return stored;
        }

        template <class Texel>
        void paint(CommandPool& pool, const Device& device, const Image& image, std::span<const Texel> texels)
        {
            const Buffer staging
                = Buffer::staging(device, texels.size_bytes(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "test");
            staging.writeAt(0, texels);
            pool.submitAndWait([&](VkCommandBuffer commands) {
                image.transition(commands, Use::sUndefined, Use::sCopyWrite);
                const VkBufferImageCopy region = wholeLevel(0, 0, VkExtent3D{ sWidth, sHeight, 1 });
                vkCmdCopyBufferToImage(
                    commands, staging.getHandle(), image.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                image.transition(commands, Use::sCopyWrite, Use::sComputeRead);
            });
        }

        std::vector<std::uint16_t> halvesOf(std::span<const float> values)
        {
            std::vector<std::uint16_t> halves;
            halves.reserve(values.size());
            for (const float value : values)
                halves.push_back(toHalf(value));
            return halves;
        }

        /// **The guides RR is handed, at every pixel, in the frame's own order.** The sky stands at
        /// RR's defaults — diffuse albedo a half, no specular albedo, no normal, no roughness, no depth
        /// — and keeps its motion; a near-black albedo, under half a step of eight bits in every
        /// channel, is nought, and one channel above it keeps the albedo whole; a surface with no lobe
        /// has no specular albedo and a roughness of one; a lobed one keeps its split-sum albedo and its
        /// roughness; every normal is the one the surface channel coded, to its step. The depth is the
        /// reversed one every upscaler is handed, `near / (distance · cos)`: the two pixels either side
        /// of the middle lean alike, so the one twice as far reads half the depth.
        TEST_F(RtxRayReconstructionGuidesTest, theGuidesAreTheChannelsInTheFramesOrderWithTheSkyAtRrsDefaults)
        {
            const Device& device = getDevice();
            CommandPool& pool = getPool();

            std::vector<float> surface;
            std::vector<float> motion;
            std::vector<float> albedo;
            std::vector<float> lobeAlbedo;
            std::vector<float> specular;
            for (const Pixel& pixel : sPixels)
            {
                surface.insert(surface.end(),
                    { Shaders::packSurfaceNormal(pixel.mNormal),
                        Shaders::packSurfaceDistance(pixel.mDistance, false) });
                motion.insert(motion.end(), { pixel.mMotion.x(), pixel.mMotion.y(), 0.0f, 0.0f });
                albedo.insert(albedo.end(), { pixel.mAlbedo.x(), pixel.mAlbedo.y(), pixel.mAlbedo.z(), 0.0f });
                lobeAlbedo.insert(
                    lobeAlbedo.end(), { pixel.mLobeAlbedo.x(), pixel.mLobeAlbedo.y(), pixel.mLobeAlbedo.z(), 0.0f });
                specular.insert(specular.end(), { 0.0f, 0.0f, 0.0f, pixel.mRoughness });
            }

            const VkExtent2D extent{ sWidth, sHeight };
            const auto image = [&](const Shaders::StorageFormat format, const char* name) {
                return Testing::makeTestImage(device, extent, toVulkanFormat(format), name);
            };
            const Image surfaceImage = image(GBUFFER_SURFACE, "test-surface");
            const Image motionImage = image(GBUFFER_MOTION, "test-motion");
            const Image albedoImage = image(GBUFFER_ALBEDO, "test-albedo");
            const Image lobeAlbedoImage = image(GBUFFER_ALBEDO, "test-specular-albedo");
            const Image specularImage = image(GBUFFER_RADIANCE_SUMMED, "test-specular");
            paint<float>(pool, device, surfaceImage, inFields<float>(surface, 2));
            paint<std::uint16_t>(pool, device, motionImage, halvesOf(inFields<float>(motion, 4)));
            paint<std::uint16_t>(pool, device, albedoImage, halvesOf(inFields<float>(albedo, 4)));
            paint<std::uint16_t>(pool, device, lobeAlbedoImage, halvesOf(inFields<float>(lobeAlbedo, 4)));
            paint<float>(pool, device, specularImage, inFields<float>(specular, 4));

            const Image depth = image(RR_DEPTH_FORMAT, "test-rr-depth");
            const Image motionOut = image(RR_MOTION_FORMAT, "test-rr-motion");
            const Image diffuseOut = image(RR_GUIDE_FORMAT, "test-rr-diffuse");
            const Image specularOut = image(RR_GUIDE_FORMAT, "test-rr-specular");
            const Image normalOut = image(RR_GUIDE_FORMAT, "test-rr-normal");

            const Shaders::VisibilityConstants camera
                = Testing::makeCamera(osg::Vec3f(), osg::Vec3f(0.0f, 1.0f, 0.0f), 60.0f, sWidth, sHeight, 1000.0f);
            Shaders::RrGuideConstants constants{
                .mInputs = { .mEyes = camera.mEyes, .mNear = sUpscaleNear, .mFrame = sFrame },
                .mLobed = 1u,
            };
            const Buffer block = Buffer::staging(device, sizeof(constants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, "test");

            const ComputePipeline<NoConstants> guides(device, sBindings, {}, "rrguides.comp.spv", "test-rr-guides");
            const auto dispatchGuides = [&] {
                block.writeAt(0, std::span<const Shaders::RrGuideConstants>(&constants, 1));
                pool.submitAndWait([&](VkCommandBuffer commands) {
                    for (const Image* written : { &depth, &motionOut, &diffuseOut, &specularOut, &normalOut })
                        written->transition(commands, Use::sUndefined, Use::sComputeWrite);
                    DescriptorWrites writes(guides);
                    writes.image(Shaders::RR_GUIDES_BIND_SURFACE, surfaceImage.describeStorage());
                    writes.image(Shaders::RR_GUIDES_BIND_MOTION, motionImage.describeStorage());
                    writes.image(Shaders::RR_GUIDES_BIND_ALBEDO, albedoImage.describeStorage());
                    writes.image(Shaders::RR_GUIDES_BIND_SPECULAR_ALBEDO, lobeAlbedoImage.describeStorage());
                    writes.image(Shaders::RR_GUIDES_BIND_SPECULAR, specularImage.describeStorage());
                    writes.image(Shaders::RR_GUIDES_BIND_DEPTH, depth.describeStorage());
                    writes.image(Shaders::RR_GUIDES_BIND_MOTION_OUT, motionOut.describeStorage());
                    writes.image(Shaders::RR_GUIDES_BIND_DIFFUSE_ALBEDO, diffuseOut.describeStorage());
                    writes.image(Shaders::RR_GUIDES_BIND_SPECULAR_ALBEDO_OUT, specularOut.describeStorage());
                    writes.image(Shaders::RR_GUIDES_BIND_NORMAL_ROUGHNESS, normalOut.describeStorage());
                    writes.buffer(Shaders::RR_GUIDES_BIND_CONSTANTS, block.describe(0, sizeof(constants)));
                    dispatch(commands, guides, writes, NoConstants{},
                        Groups::covering(sWidth, sHeight, Shaders::RR_GUIDES_WORKGROUP));
                    for (const Image* written : { &depth, &motionOut, &diffuseOut, &specularOut, &normalOut })
                        written->transition(commands, Use::sComputeWrite, Use::sComputeRead);
                });
            };
            dispatchGuides();

            std::vector<float> depths;
            depth.readFloats(VK_IMAGE_LAYOUT_GENERAL, depths);
            const std::vector<float> motions = Testing::readHalves(motionOut);
            const std::vector<float> diffuses = Testing::readHalves(diffuseOut);
            const std::vector<float> speculars = Testing::readHalves(specularOut);
            const std::vector<float> normals = Testing::readHalves(normalOut);
            ASSERT_EQ(depths.size(), sWidth);
            ASSERT_EQ(motions.size(), 2 * sWidth);
            ASSERT_EQ(diffuses.size(), 4 * sWidth);

            const auto rgb = [](const std::vector<float>& values, const std::uint32_t x) {
                return osg::Vec3f(values[4 * x], values[4 * x + 1], values[4 * x + 2]);
            };
            const auto half = [](const float value) { return fromHalf(toHalf(value)); };
            const auto halfOf = [&](const osg::Vec3f& value) {
                return osg::Vec3f(half(value.x()), half(value.y()), half(value.z()));
            };

            for (std::uint32_t x = 0; x < sWidth; ++x)
            {
                const Pixel& pixel = sPixels[x];
                EXPECT_EQ(motions[2 * x], half(pixel.mMotion.x())) << x;
                EXPECT_EQ(motions[2 * x + 1], half(pixel.mMotion.y())) << x;
            }

            // The sky.
            EXPECT_EQ(depths[0], 0.0f);
            EXPECT_EQ(rgb(diffuses, 0), osg::Vec3f(0.5f, 0.5f, 0.5f));
            EXPECT_EQ(rgb(speculars, 0), osg::Vec3f());
            EXPECT_EQ(rgb(normals, 0), osg::Vec3f());
            EXPECT_EQ(normals[3], 0.0f);

            // The near-black albedo, and one channel of a dark red above the floor.
            EXPECT_EQ(rgb(diffuses, 1), osg::Vec3f());
            EXPECT_EQ(rgb(diffuses, 4), halfOf(osg::Vec3f(0.003f, 0.0f, 0.0f)));

            // No lobe, a lobe, and a rough lobe.
            EXPECT_EQ(rgb(diffuses, 2), halfOf(sPixels[2].mAlbedo));
            EXPECT_EQ(rgb(speculars, 2), osg::Vec3f()) << "a surface with no lobe reflects nothing to RR";
            EXPECT_EQ(normals[4 * 2 + 3], 1.0f) << "and is as rough as a lobe can be";
            EXPECT_EQ(rgb(speculars, 3), halfOf(sPixels[3].mLobeAlbedo));
            EXPECT_EQ(normals[4 * 3 + 3], 0.25f);
            EXPECT_EQ(rgb(speculars, 5), osg::Vec3f(0.5f, 0.5f, 0.5f));
            EXPECT_EQ(normals[4 * 5 + 3], 1.0f);

            // Every normal the surface channel coded, to its step.
            for (std::uint32_t x = 1; x < sWidth; ++x)
            {
                const Shaders::vec3 coded
                    = Shaders::unpackSurfaceNormal(Shaders::packSurfaceNormal(sPixels[x].mNormal));
                EXPECT_EQ(rgb(normals, x), halfOf(osg::Vec3f(coded.x(), coded.y(), coded.z()))) << x;
            }

            // The depth: nought at the sky, and inverse to the distance along a ray.
            for (std::uint32_t x = 1; x < sWidth; ++x)
            {
                EXPECT_GT(depths[x], 0.0f) << x;
                EXPECT_GE(depths[x] * sPixels[x].mDistance, sUpscaleNear * 0.9999f)
                    << x << ": a ray leaning off the axis meets the depth nearer";
            }
            EXPECT_NEAR(depths[2], 2.0f * depths[3], 1e-6f * depths[2])
                << "twice as far along a ray leaning as far reads half the depth";

            // **A frame no surface of which can have a lobe reads neither lobe channel**: every surface
            // stands with no specular albedo and as rough as a lobe can be, the lobed pixels' channels
            // unread, and the sky at its defaults.
            constants.mLobed = 0u;
            dispatchGuides();
            const std::vector<float> plainSpeculars = Testing::readHalves(specularOut);
            const std::vector<float> plainNormals = Testing::readHalves(normalOut);
            for (std::uint32_t x = 0; x < sWidth; ++x)
            {
                EXPECT_EQ(rgb(plainSpeculars, x), osg::Vec3f()) << x;
                EXPECT_EQ(plainNormals[4 * x + 3], x == 0 ? 0.0f : 1.0f) << x;
            }

            device.waitIdle();
            device.collectIdle();
        }
    }
}
