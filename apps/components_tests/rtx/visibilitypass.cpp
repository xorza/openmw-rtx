#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/buffer.hpp>
#include <components/rtx/commands.hpp>
#include <components/rtx/error.hpp>
#include <components/rtx/image.hpp>
#include <components/rtx/sceneacceleration.hpp>
#include <components/rtx/scenebuffers.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtx/texture.hpp>
#include <components/rtx/visibilitypass.hpp>

#include "harness.hpp"

namespace Rtx
{
    namespace
    {
        /// OpenSceneGraph's transform and Vulkan's must move a point to the same place.
        ///
        /// OSG multiplies a row vector on the left and Vulkan a column vector on the right, so the
        /// conversion is a transpose with the translation moved from the last row to the last
        /// column. Getting it wrong mirrors the world about its diagonal, which symmetrical
        /// architecture hides well enough to survive being looked at.
        TEST(RtxTransformTest, theVulkanTransformMovesAPointWhereOpenSceneGraphWould)
        {
            osg::Matrixf matrix = osg::Matrixf::scale(2.0f, 2.0f, 2.0f)
                * osg::Matrixf::rotate(osg::DegreesToRadians(37.0f), osg::Vec3f(0.3f, -0.5f, 0.8f))
                * osg::Matrixf::translate(11.0f, -23.0f, 5.0f);

            const osg::Vec3f point(3.0f, -5.0f, 7.0f);
            const osg::Vec3f expected = point * matrix;

            const VkTransformMatrixKHR transform = toVulkanTransform(matrix);
            for (int row = 0; row < 3; ++row)
            {
                const float actual = transform.matrix[row][0] * point.x() + transform.matrix[row][1] * point.y()
                    + transform.matrix[row][2] * point.z() + transform.matrix[row][3];
                EXPECT_NEAR(actual, expected[row], 1e-3f) << "row " << row;
            }
        }

        TEST(RtxCameraTest, theBasisIsRightHandedAboutTheWorldsUpAxis)
        {
            // Looking along +Y from the origin, 90 degrees of vertical field of view, square image:
            // the half-extents at unit distance are both tan(45) = 1.
            const Shaders::VisibilityConstants camera
                = makeCamera(osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 90.0f, 100, 100, 1000.0f);

            EXPECT_NEAR(camera.mForward.y(), 1.0f, 1e-5f);
            EXPECT_NEAR(camera.mRight.x(), 1.0f, 1e-5f);
            EXPECT_NEAR(camera.mUp.z(), 1.0f, 1e-5f);
        }

        TEST(RtxCameraTest, aWiderImageWidensTheHorizontalExtentAndLeavesTheVerticalAlone)
        {
            const Shaders::VisibilityConstants wide
                = makeCamera(osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 90.0f, 200, 100, 1000.0f);

            EXPECT_NEAR(wide.mRight.x(), 2.0f, 1e-5f);
            EXPECT_NEAR(wide.mUp.z(), 1.0f, 1e-5f);
        }

        /// These come off a command line, so they are input and get a message rather than an assert
        /// that a release build would drop on the floor — leaving a normalised zero vector to fill
        /// the image with NaN and report nothing.
        TEST(RtxCameraTest, aCameraWithNoBasisIsRejectedRatherThanProducingNaN)
        {
            EXPECT_THROW(
                makeCamera(osg::Vec3f(1.0f, 2.0f, 3.0f), osg::Vec3f(1.0f, 2.0f, 3.0f), 60.0f, 64, 64, 1.0f), Error);

            EXPECT_THROW(
                makeCamera(osg::Vec3f(0.0f, 0.0f, 100.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, 64, 64, 1.0f), Error);
        }

        /// What the shader writes for a linear value, so a test can name the byte it expects.
        std::uint8_t encodeSrgb(float linear)
        {
            const float encoded
                = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            return static_cast<std::uint8_t>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
        }

        class RtxVisibilityTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                std::string reason;
                mHarness = Testing::getHarness(reason);
                if (mHarness == nullptr)
                    GTEST_SKIP() << reason;

                mHarness->mInstance->getValidationLog()->clear();
            }

            void TearDown() override
            {
                if (mHarness == nullptr)
                    return;

                for (const ValidationMessage& message :
                    mHarness->mInstance->getValidationLog()->getErrorsOnThisThread())
                    ADD_FAILURE() << "validation error: " << message.mText;
            }

            /// A square in the xz plane at y = 0, facing along -Y, four hundred units across.
            static SceneDesc makeWall()
            {
                const std::array positions{
                    osg::Vec3f(-200.0f, 0.0f, -200.0f),
                    osg::Vec3f(200.0f, 0.0f, -200.0f),
                    osg::Vec3f(200.0f, 0.0f, 200.0f),
                    osg::Vec3f(-200.0f, 0.0f, 200.0f),
                };
                constexpr std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

                SceneDesc scene;
                const Index mesh = scene.addMesh(positions, {}, {}, indices);
                scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::identity(), .mMesh = mesh });
                return scene;
            }

            /// Renders `scene` at `size` square and returns how many primary rays hit.
            std::uint32_t countHits(const SceneDesc& scene, const TextureArray& textures,
                const Shaders::VisibilityConstants& camera, std::uint32_t size, std::vector<std::uint8_t>& pixels)
            {
                Device& device = *mHarness->mDevice;
                CommandPool pool(device);
                const SceneAcceleration acceleration(device, pool, scene);
                const SceneBuffers buffers(device, pool, scene, acceleration.getIndices());

                const VisibilityPass pass(device, Testing::getShaderDirectory(), textures.getLayout());
                const VisibilityInputs inputs{
                    .mScene = acceleration.getTopLevel(),
                    .mBuffers = &buffers,
                    .mTextures = textures.getSet(),
                };

                Image target(device, size, size, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

                const Buffer hits(device, sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                *static_cast<std::uint32_t*>(hits.map()) = 0;
                hits.unmap();

                pool.submitAndWait([&](VkCommandBuffer commands) {
                    target.transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                    pass.record(commands, inputs, target, hits, camera);
                });

                pixels = target.read(pool, VK_IMAGE_LAYOUT_GENERAL);

                const std::uint32_t count = *static_cast<const std::uint32_t*>(hits.map());
                hits.unmap();
                return count;
            }

            /// For the scenes whose quads carry no material and never index the array.
            TextureArray& noTextures()
            {
                if (mNoTextures == nullptr)
                    mNoTextures = std::make_unique<TextureArray>(*mHarness->mDevice, std::vector<Texture>{});

                return *mNoTextures;
            }

            Testing::Harness* mHarness = nullptr;
            std::unique_ptr<TextureArray> mNoTextures;
        };

        /// A wall bigger than the field of view leaves no room for sky.
        ///
        /// At a hundred units from a sixty-degree camera the frame is 2 * 100 * tan(30) = 115 units
        /// tall; the wall is four hundred. Every ray must land on it, so the answer is exact rather
        /// than a threshold.
        ///
        /// The colour is exact too. These quads carry no state set, so they get the untextured
        /// material: a linear albedo of 0.5, which the shader encodes on the way out as
        /// 1.055 * 0.5^(1/2.4) - 0.055 = 0.735, or 187 of 255.
        TEST_F(RtxVisibilityTest, aWallLargerThanTheFrameIsHitByEveryRay)
        {
            constexpr std::uint32_t size = 64;
            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);
            camera.mShowAlbedo = 1u;

            std::vector<std::uint8_t> pixels;
            EXPECT_EQ(countHits(makeWall(), noTextures(), camera, size, pixels), size * size);

            ASSERT_EQ(pixels.size(), std::size_t{ size } * size * 4);
            for (std::size_t i = 0; i < pixels.size(); i += 4)
            {
                ASSERT_NEAR(pixels[i], 187, 1) << "red at pixel " << i / 4;
                ASSERT_NEAR(pixels[i + 1], 187, 1) << "green at pixel " << i / 4;
                ASSERT_NEAR(pixels[i + 2], 187, 1) << "blue at pixel " << i / 4;
            }
        }

        /// The mip chain a ray cone selects from, at a distance chosen so the answer is a whole
        /// number.
        ///
        /// Sixty-four texels across a quad that exactly fills a sixty-degree frame at a hundred
        /// units: one texel per pixel, so the cone is one texel wide and the level is zero. The
        /// arithmetic, which the shader repeats:
        ///
        ///   spread    = atan(2 * tan(30) / 64)  = 0.0180402 radians per pixel
        ///   coneWidth = spread * 100            = 1.80402 units
        ///   texelArea = 1 * 64 * 64             = 4096      (the shader's doubled form)
        ///   worldArea = |cross| = 115.47^2      = 13333.3   (doubled the same way, so it cancels)
        ///   lambda    = 0.5 * log2(4096/13333.3) + log2(1.80402) = -0.85138 + 0.85140 = 0
        ///
        /// Double the distance and the cone doubles, so lambda becomes exactly one. Each level is a
        /// different grey, so the level chosen is legible in a single pixel.
        TEST_F(RtxVisibilityTest, theConeReadsTheMipTheDistanceCallsFor)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::uint32_t levels = 7;
            constexpr float halfExtent = 57.735027f;

            // Level i is 40 + 30i, so no level is black and none is another's neighbour.
            std::vector<std::uint8_t> bytes;
            std::vector<MipLevel> mips;
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                const std::uint32_t extent = size >> level;
                mips.push_back(MipLevel{ static_cast<std::uint32_t>(bytes.size()), extent, extent });
                bytes.insert(
                    bytes.end(), std::size_t{ extent } * extent * 4, static_cast<std::uint8_t>(40 + 30 * level));
            }

            const TextureData data{
                .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                .mWidth = size,
                .mHeight = size,
                .mBytes = std::as_bytes(std::span(bytes)),
                .mLevels = mips,
            };

            Device& device = *mHarness->mDevice;
            CommandPool pool(device);
            std::vector<Texture> uploaded;
            uploaded.emplace_back(device, pool, data, "mip ladder");
            const TextureArray textures(device, std::move(uploaded));

            const std::array positions{
                osg::Vec3f(-halfExtent, 0.0f, -halfExtent),
                osg::Vec3f(halfExtent, 0.0f, -halfExtent),
                osg::Vec3f(halfExtent, 0.0f, halfExtent),
                osg::Vec3f(-halfExtent, 0.0f, halfExtent),
            };
            const std::array texCoords{
                osg::Vec2f(0.0f, 0.0f),
                osg::Vec2f(1.0f, 0.0f),
                osg::Vec2f(1.0f, 1.0f),
                osg::Vec2f(0.0f, 1.0f),
            };
            constexpr std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

            SceneDesc scene;
            const Index mesh = scene.addMesh(positions, {}, texCoords, indices);
            const Index material
                = scene.addMaterial(Material{ .mDiffuse = scene.addTexture(VFS::Path::NormalizedView("mip.dds")) });
            scene.addInstance(
                MeshInstance{ .mTransform = osg::Matrixf::identity(), .mMesh = mesh, .mMaterial = material });

            const auto centreOf = [](const std::vector<std::uint8_t>& pixels) {
                return pixels[(std::size_t{ size / 2 } * size + size / 2) * 4];
            };

            const auto renderAt = [&](float distance) {
                Shaders::VisibilityConstants camera = makeCamera(
                    osg::Vec3f(0.0f, -distance, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                camera.mShowAlbedo = 1u;

                std::vector<std::uint8_t> pixels;
                countHits(scene, textures, camera, size, pixels);
                return centreOf(pixels);
            };

            // Within a byte, because the claim is which level was read and the levels are twenty-odd
            // bytes apart once encoded — no rounding difference between the shader's transfer
            // function and this one can make a level look like its neighbour. Exact equality would
            // fail on the third, whose encoded value happens to land on 207.51.
            EXPECT_NEAR(renderAt(100.0f), encodeSrgb(40.0f / 255.0f), 1);
            EXPECT_NEAR(renderAt(200.0f), encodeSrgb(70.0f / 255.0f), 1);

            // And the far end of the ladder, so a shader that clamped at level one would be caught.
            EXPECT_NEAR(renderAt(1600.0f), encodeSrgb(160.0f / 255.0f), 1);
        }

        /// Ground: layers summed by their masks, at the weights the mask grid names.
        ///
        /// Two layers over one quad, each a solid colour, with a mask two weights wide: layer zero
        /// is [1, 0] and layer one [0, 1]. A mask samples at `u * width - 0.5`, so texel centres sit
        /// at u = 0.25 and u = 0.75 and the weight between them is a straight ramp — pure layer zero
        /// left of the first centre, pure layer one right of the second, and exactly half of each in
        /// the middle. Those three points are what this checks, because they are the ones the
        /// arithmetic pins: 0.5 of a linear one encodes to 1.055 * 0.5^(1/2.4) - 0.055, or 188.
        TEST_F(RtxVisibilityTest, groundSumsItsLayersByTheWeightsItsMasksName)
        {
            constexpr std::uint32_t size = 64;
            constexpr float halfExtent = 57.735027f;

            // Two solid textures and one two-texel strip, all one level so nothing but the layer
            // arithmetic can move a byte.
            const auto makeSolid = [](std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
                return std::array<std::uint8_t, 4>{ red, green, blue, 255 };
            };
            const std::array<std::uint8_t, 4> redTexel = makeSolid(255, 0, 0);
            const std::array<std::uint8_t, 4> greenTexel = makeSolid(0, 255, 0);
            // Sixty-four texels for sixty-four columns, so every pixel samples exactly one texel
            // centre and no filtering weight can enter the answer. Green for the first half, blue
            // for the second.
            std::array<std::uint8_t, size * 4> strip{};
            for (std::uint32_t texel = 0; texel < size; ++texel)
                strip[texel * 4 + (texel < size / 2 ? 1 : 2)] = 255;

            const MipLevel one{ 0, 1, 1 };
            const MipLevel wide{ 0, size, 1 };
            const auto describe
                = [](VkFormat format, std::uint32_t width, std::span<const std::uint8_t> bytes, const MipLevel& level) {
                      return TextureData{
                          .mFormat = format,
                          .mWidth = width,
                          .mHeight = 1,
                          .mBytes = std::as_bytes(bytes),
                          .mLevels = std::span(&level, 1),
                      };
                  };

            Device& device = *mHarness->mDevice;
            CommandPool pool(device);
            std::vector<Texture> uploaded;
            uploaded.emplace_back(device, pool, describe(VK_FORMAT_R8G8B8A8_UNORM, 1, redTexel, one), "red");
            uploaded.emplace_back(device, pool, describe(VK_FORMAT_R8G8B8A8_UNORM, 1, greenTexel, one), "green");
            uploaded.emplace_back(
                device, pool, describe(VK_FORMAT_R8G8B8A8_UNORM, size, strip, wide), "green then blue");
            const TextureArray textures(device, std::move(uploaded));

            const std::array positions{
                osg::Vec3f(-halfExtent, 0.0f, -halfExtent),
                osg::Vec3f(halfExtent, 0.0f, -halfExtent),
                osg::Vec3f(halfExtent, 0.0f, halfExtent),
                osg::Vec3f(-halfExtent, 0.0f, halfExtent),
            };
            const std::array texCoords{
                osg::Vec2f(0.0f, 0.0f),
                osg::Vec2f(1.0f, 0.0f),
                osg::Vec2f(1.0f, 1.0f),
                osg::Vec2f(0.0f, 1.0f),
            };
            constexpr std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };
            constexpr std::array<float, 2> firstMask{ 1.0f, 0.0f };
            constexpr std::array<float, 2> secondMask{ 0.0f, 1.0f };

            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);
            camera.mShowAlbedo = 1u;

            /// @param second the texture slot and diffuse transform of the layer on the right.
            const auto render = [&](Index second, const osg::Vec4f& secondTransform) {
                SceneDesc scene;
                const Index mesh = scene.addMesh(positions, {}, texCoords, indices);
                scene.addTexture(VFS::Path::NormalizedView("red.dds"));
                scene.addTexture(VFS::Path::NormalizedView("green.dds"));
                scene.addTexture(VFS::Path::NormalizedView("strip.dds"));

                Material material;
                material.mLayerOffset = 0;
                material.mLayerCount = 2;
                scene.addLayer(MaterialLayer{
                    .mDiffuse = 0,
                    .mMaskOffset = scene.addMask(firstMask),
                    .mMaskWidth = 2,
                    .mMaskHeight = 1,
                });
                scene.addLayer(MaterialLayer{
                    .mDiffuse = second,
                    .mMaskOffset = scene.addMask(secondMask),
                    .mMaskWidth = 2,
                    .mMaskHeight = 1,
                    .mDiffuseTransform = secondTransform,
                });

                scene.addInstance(MeshInstance{
                    .mTransform = osg::Matrixf::identity(), .mMesh = mesh, .mMaterial = scene.addMaterial(material) });

                std::vector<std::uint8_t> pixels;
                EXPECT_EQ(countHits(scene, textures, camera, size, pixels), size * size);
                return pixels;
            };

            const std::vector<std::uint8_t> ramp = render(1, osg::Vec4f(1.0f, 1.0f, 0.0f, 0.0f));

            // Column c samples u = (c + 0.5) / 64, so the two texel centres fall on columns 15.5 and
            // 47.5 and the middle of the ramp on column 31.5. Sampling either side of a boundary
            // would land on a value the ramp only reaches between pixels.
            const auto at = [&](const std::vector<std::uint8_t>& pixels, std::uint32_t column) {
                return &pixels[(std::size_t{ size / 2 } * size + column) * 4];
            };

            EXPECT_EQ(at(ramp, 0)[0], 255) << "pure first layer, red";
            EXPECT_EQ(at(ramp, 0)[1], 0);
            EXPECT_EQ(at(ramp, 63)[0], 0) << "pure second layer, green";
            EXPECT_EQ(at(ramp, 63)[1], 255);

            // Columns 31 and 32 straddle the halfway point by half a pixel each, so neither is an
            // even split and the two are mirror images. Column 31 samples u = 31.5 / 64 = 0.49219,
            // which is 0.48438 of the way from the first texel centre to the second, so the second
            // layer weighs that and the first weighs 0.51563. Encoded:
            //
            //   1.055 * 0.51563^(1/2.4) - 0.055 = 0.74547, or 190 of 255
            //   1.055 * 0.48438^(1/2.4) - 0.055 = 0.72503, or 185 of 255
            EXPECT_EQ(at(ramp, 31)[0], 190) << "the first layer, three sixty-fourths past centre";
            EXPECT_EQ(at(ramp, 31)[1], 185);
            EXPECT_EQ(at(ramp, 32)[0], 185) << "and the mirror of it on the other side";
            EXPECT_EQ(at(ramp, 32)[1], 190);

            // Outside the two centres the ramp is flat, which is the clamp doing its work: a mask
            // that wrapped would fold the far layer back over the near one at both edges.
            EXPECT_EQ(at(ramp, 15)[0], 255) << "still pure at the first texel centre";
            EXPECT_EQ(at(ramp, 48)[1], 255) << "and at the second";

            // The layer's own texture transform, proved by moving it under a fixed pixel. Column 63
            // samples u = 0.99219, which on the sixty-four-texel strip is texel 63's centre — the
            // blue half. Half a unit of offset puts the same pixel on texel 31, the green half, and
            // both are exact centres so the answer is a texel rather than a blend of two.
            const std::vector<std::uint8_t> blue = render(2, osg::Vec4f(1.0f, 1.0f, 0.0f, 0.0f));
            const std::vector<std::uint8_t> green = render(2, osg::Vec4f(1.0f, 1.0f, -0.5f, 0.0f));

            EXPECT_EQ(at(blue, 63)[2], 255) << "the strip's far half";
            EXPECT_EQ(at(blue, 63)[1], 0);
            EXPECT_EQ(at(green, 63)[1], 255) << "and its near half, half a coordinate back";
            EXPECT_EQ(at(green, 63)[2], 0);
        }

        /// The sun, which is one direction everywhere and casts a shadow to the end of the world.
        ///
        /// The wall's normal is (0, -1, 0), so a sun travelling straight along it meets it square and
        /// the whole answer is the irradiance: 0.5 albedo times 2.0 over pi is 0.318310 linear, which
        /// encodes to 1.055 * 0.318310^(1/2.4) - 0.055 = 0.599797, or 153 of 255.
        TEST_F(RtxVisibilityTest, theSunLightsWhatItFacesAndTheOccluderTakesItAway)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;

            const Shaders::VisibilityConstants base = makeCamera(
                osg::Vec3f(100.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);

            const std::array occluder{
                osg::Vec3f(-10.0f, -25.0f, -10.0f),
                osg::Vec3f(10.0f, -25.0f, -10.0f),
                osg::Vec3f(10.0f, -25.0f, 10.0f),
                osg::Vec3f(-10.0f, -25.0f, 10.0f),
            };
            constexpr std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

            const auto render = [&](const osg::Vec3f& direction, const osg::Vec3f& irradiance, bool blocked,
                                    const osg::Vec3f& ambient = osg::Vec3f()) {
                SceneDesc scene = makeWall();
                if (blocked)
                    scene.addInstance(MeshInstance{
                        .mTransform = osg::Matrixf::identity(), .mMesh = scene.addMesh(occluder, {}, {}, indices) });

                Shaders::VisibilityConstants camera = base;
                camera.mSunDirection = direction;
                camera.mSunIrradiance = irradiance;
                camera.mAmbient = ambient;

                std::vector<std::uint8_t> pixels;
                EXPECT_GT(countHits(scene, noTextures(), camera, size, pixels), 0u);
                return pixels[centre];
            };

            // Travelling along +Y, which is straight into the wall's face.
            const osg::Vec3f onto(0.0f, 1.0f, 0.0f);
            const osg::Vec3f bright(2.0f, 2.0f, 2.0f);

            EXPECT_EQ(render(onto, bright, false), 153) << "square to the sun";
            EXPECT_EQ(render(onto, bright, true), 0) << "and with something standing in the way";

            // A sun travelling out of the wall rather than into it reaches its back, and is dropped
            // rather than arithmetically applied. Asserted against the ambient, because a negative
            // contribution clamps to black as well and the two only tell apart against something:
            // 0.5 * 0.4 = 0.2 linear, which encodes to 124 of 255.
            EXPECT_EQ(render(-onto, bright, false, osg::Vec3f(0.4f, 0.4f, 0.4f)), 124)
                << "a sun behind the wall lights nothing";

            // Half the irradiance is nowhere near half the byte, because the encoding is not
            // linear: 0.5 * 1.0 / pi = 0.159155, which encodes to 0.435542, or 111 of 255.
            EXPECT_EQ(render(onto, osg::Vec3f(1.0f, 1.0f, 1.0f), false), 111) << "and half as much sun";

            // At sixty degrees off square the cosine is exactly a half, so this is the same radiance
            // the half-irradiance case gave — the same 111, reached the other way.
            const osg::Vec3f slanted(std::sqrt(3.0f) * 0.5f, 0.5f, 0.0f);
            EXPECT_EQ(render(slanted, bright, false), 111) << "or the same again from a slant";
        }

        /// A ray that hits nothing comes back with the sky the weather named, not a constant.
        TEST_F(RtxVisibilityTest, theSkyIsTheWeathersOwnColourAndRunsFromHorizonToZenith)
        {
            constexpr std::uint32_t size = 33;

            // Facing straight up, so the centre pixel looks at the zenith and the frame's edge looks
            // sixty degrees off it. Nothing is placed, so every ray misses.
            Shaders::VisibilityConstants camera
                = makeCamera(osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 60.0f, size, size, 10000.0f);
            camera.mSkyHorizon = osg::Vec3f(1.0f, 0.0f, 0.0f);
            camera.mSkyZenith = osg::Vec3f(0.0f, 0.0f, 1.0f);

            SceneDesc scene = makeWall();
            std::vector<std::uint8_t> pixels;
            countHits(scene, noTextures(), camera, size, pixels);

            // The camera looks level, so the middle row's rays are horizontal: z of zero, which is
            // the horizon end of the mix exactly. Pure red, and no blue at all.
            const std::size_t middle = (std::size_t{ size / 2 } * size + size / 2) * 4;
            EXPECT_EQ(pixels[middle], 255) << "the horizon colour, undiluted";
            EXPECT_EQ(pixels[middle + 2], 0);

            // The top row tilts up by tan(30) of the half-frame, so its z is sin of that angle and
            // the mix has moved toward the zenith. Only the direction of the move is asserted: the
            // exact angle is the camera's business and has its own test.
            const std::size_t top = std::size_t{ size / 2 } * 4;
            EXPECT_LT(pixels[top], 255) << "less horizon overhead";
            EXPECT_GT(pixels[top + 2], 0) << "and some zenith";
        }

        /// A glow, which the engine treats as two different things and so does this.
        ///
        /// The emissive **colour** joins the light and is multiplied by the texture, so a surface
        /// glows *with its own texture in it*. The emissive **map** is added past the albedo, so it
        /// glows through whatever the surface is made of. Getting either backwards is visible: a
        /// mushroom cap comes out flat white, or a map on a coloured surface goes black.
        TEST_F(RtxVisibilityTest, aGlowJoinsTheLightAndAGlowingMapIsAddedPastIt)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;
            constexpr float halfExtent = 57.735027f;

            const std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<std::uint8_t, 4> green{ 0, 255, 0, 255 };
            const std::array<std::uint8_t, 4> dimRed{ 64, 0, 0, 255 };

            const MipLevel one{ 0, 1, 1 };
            const auto describe = [&one](std::span<const std::uint8_t> bytes) {
                return TextureData{
                    .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                    .mWidth = 1,
                    .mHeight = 1,
                    .mBytes = std::as_bytes(bytes),
                    .mLevels = std::span(&one, 1),
                };
            };

            Device& device = *mHarness->mDevice;
            CommandPool pool(device);
            std::vector<Texture> uploaded;
            uploaded.emplace_back(device, pool, describe(white), "white");
            uploaded.emplace_back(device, pool, describe(green), "green");
            uploaded.emplace_back(device, pool, describe(dimRed), "dim red");
            const TextureArray textures(device, std::move(uploaded));

            const std::array positions{
                osg::Vec3f(-halfExtent, 0.0f, -halfExtent),
                osg::Vec3f(halfExtent, 0.0f, -halfExtent),
                osg::Vec3f(halfExtent, 0.0f, halfExtent),
                osg::Vec3f(-halfExtent, 0.0f, halfExtent),
            };
            const std::array texCoords{
                osg::Vec2f(0.0f, 0.0f),
                osg::Vec2f(1.0f, 0.0f),
                osg::Vec2f(1.0f, 1.0f),
                osg::Vec2f(0.0f, 1.0f),
            };
            constexpr std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

            // Nothing lights this scene at all: no lamp, no sun, no ambient. Whatever comes back is
            // the surface's own glow and nothing else.
            const Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);

            const auto render = [&](Index diffuse, Index emissiveMap, const osg::Vec3f& emissiveColour) {
                SceneDesc scene;
                const Index mesh = scene.addMesh(positions, {}, texCoords, indices);
                scene.addTexture(VFS::Path::NormalizedView("white.dds"));
                scene.addTexture(VFS::Path::NormalizedView("green.dds"));
                scene.addTexture(VFS::Path::NormalizedView("red.dds"));

                const Index material = scene.addMaterial(Material{
                    .mDiffuse = diffuse,
                    .mEmissive = emissiveMap,
                    .mEmissiveColour = emissiveColour,
                });
                scene.addInstance(
                    MeshInstance{ .mTransform = osg::Matrixf::identity(), .mMesh = mesh, .mMaterial = material });

                std::vector<std::uint8_t> pixels;
                EXPECT_EQ(countHits(scene, textures, camera, size, pixels), size * size);
                return std::array<std::uint8_t, 3>{ pixels[centre], pixels[centre + 1], pixels[centre + 2] };
            };

            // A quarter of a glow on a white surface. The scale carries the original's "one is a
            // fully lit surface" onto this renderer's, where the sun is eight and the sky a fifth of
            // that: 0.25 * 1.6 = 0.4 linear, and 1.055 * 0.4^(1/2.4) - 0.055 = 0.66514, or 170.
            const osg::Vec3f quarter(0.25f, 0.25f, 0.25f);
            EXPECT_EQ(render(0, sNoIndex, quarter)[0], 170) << "a glow on white";

            // The same glow on a texture with no red in it keeps the texture's colour, because the
            // glow goes through the albedo. Added past it, the surface would come back white.
            const std::array<std::uint8_t, 3> onGreen = render(1, sNoIndex, quarter);
            EXPECT_EQ(onGreen[1], 170) << "the same glow, still through green";
            EXPECT_EQ(onGreen[0], 0) << "and with none of the red the white one had";

            // The map is the other way round: red light off a green surface. Through the albedo it
            // would be black, since green times red is nothing. 64 of 255 is 0.25098, times 1.6 is
            // 0.40157, which encodes to 170 — the same byte, reached without the texture's help.
            const std::array<std::uint8_t, 3> mapped = render(1, 2, osg::Vec3f());
            EXPECT_EQ(mapped[0], 170) << "the map's own red, undimmed by the green under it";
            EXPECT_EQ(mapped[1], 0) << "and none of the green, which nothing is lighting";
        }

        /// A masked surface in front of a wall: the ray stops on what survives the cutout and goes
        /// on through what does not.
        ///
        /// The scene is arranged so the answer is a whole number of pixels. The mask is sixteen
        /// texels across, red throughout, and transparent over its left half; the quad carrying it
        /// exactly fills a sixty-degree frame at a hundred units, so image column c samples
        /// u = (c + 0.5) / 64 and the sampler's REPEAT wrap makes both seams behave the same way.
        /// At column 31 the filter sits 0.375 of the way onto the first opaque texel and at column
        /// 32 it sits 0.625 on, so a cutoff of 0.5 falls exactly between them with a margin of
        /// 0.125 either side — far wider than the four bits of sub-texel precision Vulkan
        /// guarantees. The left half of the image is therefore the wall behind and the right half
        /// is the mask, with nothing in between.
        TEST_F(RtxVisibilityTest, aCutoutStopsARayOnItsMaskAndLetsItThroughTheHoles)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::uint32_t extent = 16;
            constexpr std::uint32_t seam = size / 2;

            std::vector<std::uint8_t> bytes(std::size_t{ extent } * extent * 4);
            for (std::uint32_t y = 0; y < extent; ++y)
                for (std::uint32_t x = 0; x < extent; ++x)
                {
                    std::uint8_t* const texel = &bytes[(std::size_t{ y } * extent + x) * 4];
                    texel[0] = 255;
                    texel[3] = x < extent / 2 ? 0 : 255;
                }

            const MipLevel level{ 0, extent, extent };
            const TextureData data{
                .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                .mWidth = extent,
                .mHeight = extent,
                .mBytes = std::as_bytes(std::span(bytes)),
                .mLevels = std::span(&level, 1),
            };

            Device& device = *mHarness->mDevice;
            CommandPool pool(device);
            std::vector<Texture> uploaded;
            uploaded.emplace_back(device, pool, data, "half-masked");
            const TextureArray textures(device, std::move(uploaded));

            // A hundred units from the camera, where the frame is 2 * 100 * tan(30) across.
            constexpr float halfExtent = 57.735027f;
            const std::array masked{
                osg::Vec3f(-halfExtent, -50.0f, -halfExtent),
                osg::Vec3f(halfExtent, -50.0f, -halfExtent),
                osg::Vec3f(halfExtent, -50.0f, halfExtent),
                osg::Vec3f(-halfExtent, -50.0f, halfExtent),
            };
            const std::array maskedUv{
                osg::Vec2f(0.0f, 0.0f),
                osg::Vec2f(1.0f, 0.0f),
                osg::Vec2f(1.0f, 1.0f),
                osg::Vec2f(0.0f, 1.0f),
            };
            constexpr std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -150.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);
            camera.mShowAlbedo = 1u;

            const auto render = [&](AlphaMode mode, float alphaRef) {
                SceneDesc scene = makeWall();
                const Index mesh = scene.addMesh(masked, {}, maskedUv, indices);
                const Index material = scene.addMaterial(Material{
                    .mDiffuse = scene.addTexture(VFS::Path::NormalizedView("mask.dds")),
                    .mAlphaRef = alphaRef,
                    .mAlphaMode = mode,
                });
                scene.addInstance(
                    MeshInstance{ .mTransform = osg::Matrixf::identity(), .mMesh = mesh, .mMaterial = material });

                std::vector<std::uint8_t> pixels;
                // Something is behind every hole, so every ray lands on one surface or the other.
                EXPECT_EQ(countHits(scene, textures, camera, size, pixels), size * size);
                return pixels;
            };

            // Red where the mask survived and grey where the wall shows through: the mask is pure
            // red, so its green is zero, and the untextured wall's albedo of 0.5 encodes to
            // 1.055 * 0.5^(1/2.4) - 0.055 = 0.73536, or 187.5 of 255 — which is why the grey is the
            // one value here given a byte of room.
            constexpr int wallGrey = 188;

            const std::vector<std::uint8_t> cutout = render(AlphaMode::Cutout, 0.5f);
            ASSERT_EQ(cutout.size(), std::size_t{ size } * size * 4);
            for (std::uint32_t row = 0; row < size; ++row)
                for (std::uint32_t column = 0; column < size; ++column)
                {
                    const std::uint8_t* const pixel = &cutout[(std::size_t{ row } * size + column) * 4];
                    if (column >= seam)
                    {
                        ASSERT_EQ(pixel[0], 255) << "red at " << column << ", " << row;
                        ASSERT_EQ(pixel[1], 0) << "green at " << column << ", " << row;
                    }
                    else
                    {
                        ASSERT_NEAR(pixel[0], wallGrey, 1) << "red at " << column << ", " << row;
                        ASSERT_NEAR(pixel[1], wallGrey, 1) << "green at " << column << ", " << row;
                    }
                }

            // A blend that named no threshold of its own is traced against the stand-in, and the
            // stand-in is the same half. Same bytes, or Morrowind's foliage — which is blended and
            // never alpha-tested — would not be cut out at all.
            EXPECT_EQ(render(AlphaMode::Blend, 0.0f), cutout);

            // And the control: the same texture on an opaque material hides the wall completely, so
            // it is the cutout doing this and not the geometry.
            const std::vector<std::uint8_t> opaque = render(AlphaMode::Opaque, 0.5f);
            for (std::size_t i = 0; i < opaque.size(); i += 4)
            {
                ASSERT_EQ(opaque[i], 255) << "red at pixel " << i / 4;
                ASSERT_EQ(opaque[i + 1], 0) << "green at pixel " << i / 4;
            }
        }

        /// A wall lit by one lamp, at the radiance the falloff says and nowhere else.
        ///
        /// The centre pixel looks straight at the origin, where the wall's normal is (0, -1, 0) and
        /// the light sits fifty units along it, so the cosine is exactly one and the whole answer is
        /// the falloff. Written out, with a reach of 500 and the untextured albedo of 0.5:
        ///
        ///   window    = 1 - (50 / 500)^4              = 0.99990
        ///   falloff   = window^2 / (50^2 + 1)         = 0.99980 / 2501 = 3.99760e-4
        ///   radiance  = 4000 * falloff / pi           = 0.508947
        ///   encoded   = 1.055 * (0.5 * 0.508947)^(1/2.4) - 0.055 = 0.54150, or 138 of 255
        ///
        /// The camera stands off the light's axis so that something can be put between the wall and
        /// the lamp without also standing in front of the wall.
        ///
        /// That cosine of one is also what pins the normal: it holds only if the plane's normal came
        /// back out of the acceleration structure unrotated and unmirrored, which symmetrical
        /// geometry otherwise hides well enough to survive being looked at.
        TEST_F(RtxVisibilityTest, aLampLightsAWallByItsFalloffAndAnObstacleTakesItAway)
        {
            // Odd, so one pixel sits exactly on the axis and its ray lands exactly on the origin.
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;

            const Shaders::VisibilityConstants base = makeCamera(
                osg::Vec3f(100.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);

            // Ten units across, a quarter of the way from the wall to the lamp: it covers the whole
            // shadow ray and none of the camera's, which passes through x = 25 at that height.
            const std::array occluder{
                osg::Vec3f(-10.0f, -25.0f, -10.0f),
                osg::Vec3f(10.0f, -25.0f, -10.0f),
                osg::Vec3f(10.0f, -25.0f, 10.0f),
                osg::Vec3f(-10.0f, -25.0f, 10.0f),
            };
            constexpr std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

            const auto render = [&](const std::optional<Light>& light, const osg::Vec3f& ambient, bool blocked) {
                SceneDesc scene = makeWall();
                if (light.has_value())
                    scene.addLight(*light);
                if (blocked)
                    scene.addInstance(MeshInstance{
                        .mTransform = osg::Matrixf::identity(), .mMesh = scene.addMesh(occluder, {}, {}, indices) });

                Shaders::VisibilityConstants camera = base;
                camera.mAmbient = ambient;
                camera.mLightCount = static_cast<std::uint32_t>(scene.getLights().size());

                std::vector<std::uint8_t> pixels;
                EXPECT_GT(countHits(scene, noTextures(), camera, size, pixels), 0u);
                return pixels[centre];
            };

            const Light lamp{
                .mPosition = osg::Vec3f(0.0f, -50.0f, 0.0f),
                .mIntensity = osg::Vec3f(4000.0f, 4000.0f, 4000.0f),
                .mReach = 500.0f,
            };

            EXPECT_EQ(render(lamp, osg::Vec3f(), false), 138) << "lit by the lamp alone";

            // The same lamp with something in the way. Nothing else lights the wall, so the pixel
            // goes to black rather than merely dimmer — which is what tells a shadow from a falloff.
            EXPECT_EQ(render(lamp, osg::Vec3f(), true), 0) << "and shadowed by the quad between them";

            // Ambient with no lamp at all: 0.5 * 0.4 = 0.2 linear, which encodes to
            // 1.055 * 0.2^(1/2.4) - 0.055 = 0.48453, or 124 of 255.
            const osg::Vec3f ambient(0.4f, 0.4f, 0.4f);
            EXPECT_EQ(render(std::nullopt, ambient, false), 124) << "ambient alone";

            // A lamp behind the wall meets it at a cosine of minus one, and is dropped rather than
            // arithmetically applied. Asserted against the ambient and not against black, because
            // black is what a negative contribution clamps to as well — the two only tell apart
            // where there is something for it to be subtracted from.
            Light behind = lamp;
            behind.mPosition = osg::Vec3f(0.0f, 50.0f, 0.0f);
            EXPECT_EQ(render(behind, ambient, false), 124) << "a lamp on the far side lights nothing";

            // The window, biting at half the reach rather than at the very end of it, where it is
            // indistinguishable from the inverse square alone:
            //
            //   window   = 1 - (50 / 100)^4      = 0.93750
            //   falloff  = 0.87891 / 2501        = 3.51422e-4
            //   radiance = 4000 * falloff / pi   = 0.447465
            //   encoded  = 1.055 * (0.5 * 0.447465)^(1/2.4) - 0.055 = 0.51035, or 130 of 255
            Light near = lamp;
            near.mReach = 100.0f;
            EXPECT_EQ(render(near, osg::Vec3f(), false), 130) << "the window taking a bite out of it";

            // And the reach as a hard limit: at exactly its own reach a lamp is skipped outright.
            Light spent = lamp;
            spent.mReach = 50.0f;
            EXPECT_EQ(render(spent, osg::Vec3f(), false), 0) << "and one whose reach ends at the wall";
        }

        /// The same scene with the camera turned around. Nothing is in front of it, so nothing is hit
        /// — the check that the pass reports geometry rather than reporting that it ran.
        TEST_F(RtxVisibilityTest, aCameraFacingAwayHitsNothingAndTheImageIsAllSky)
        {
            constexpr std::uint32_t size = 64;
            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, -200.0f, 0.0f), 60.0f, size, size, 10000.0f);

            // A sky with green in it and nothing else, so that "this is sky" and "this is the
            // untextured wall" cannot be confused: the wall is grey through every channel.
            camera.mSkyHorizon = osg::Vec3f(0.0f, 0.25f, 0.0f);
            camera.mSkyZenith = osg::Vec3f(0.0f, 0.25f, 0.0f);

            std::vector<std::uint8_t> pixels;
            EXPECT_EQ(countHits(makeWall(), noTextures(), camera, size, pixels), 0u);

            // Flat, so every pixel is the same byte: 1.055 * 0.25^(1/2.4) - 0.055 = 0.537099, which
            // is 137 of 255.
            ASSERT_EQ(pixels.size(), std::size_t{ size } * size * 4);
            for (std::size_t i = 0; i < pixels.size(); i += 4)
            {
                ASSERT_EQ(pixels[i], 0) << "red at pixel " << i / 4;
                ASSERT_EQ(pixels[i + 1], 137) << "green at pixel " << i / 4;
            }
        }

        /// A mesh moved by its instance and a mesh whose vertices were already moved must render to
        /// the same bytes.
        ///
        /// The transform path has a unit test, and a unit test is not enough on its own: every other
        /// test here places its geometry with the identity, so a transposed rotation would sail
        /// through all of them. This is the one that puts a rotation through the acceleration
        /// structure and compares the result against arithmetic done on the CPU.
        TEST_F(RtxVisibilityTest, aRotatedInstanceRendersAsIfItsVerticesHadBeenMoved)
        {
            const osg::Matrixf transform = osg::Matrixf::scale(1.5f, 1.5f, 1.5f)
                * osg::Matrixf::rotate(osg::DegreesToRadians(37.0f), osg::Vec3f(0.3f, -0.5f, 0.8f))
                * osg::Matrixf::translate(11.0f, -23.0f, 5.0f);

            const std::array local{
                osg::Vec3f(-120.0f, 0.0f, -80.0f),
                osg::Vec3f(120.0f, 0.0f, -80.0f),
                osg::Vec3f(90.0f, 0.0f, 110.0f),
                osg::Vec3f(-140.0f, 0.0f, 60.0f),
            };
            constexpr std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

            SceneDesc placedByInstance;
            placedByInstance.addInstance(
                MeshInstance{ .mTransform = transform, .mMesh = placedByInstance.addMesh(local, {}, {}, indices) });

            std::array<osg::Vec3f, 4> moved{};
            for (std::size_t i = 0; i < local.size(); ++i)
                moved[i] = local[i] * transform;

            SceneDesc placedByVertex;
            placedByVertex.addInstance(MeshInstance{
                .mTransform = osg::Matrixf::identity(), .mMesh = placedByVertex.addMesh(moved, {}, {}, indices) });

            constexpr std::uint32_t size = 64;
            const osg::Vec3f centre(11.0f, -23.0f, 5.0f);
            const Shaders::VisibilityConstants camera
                = makeCamera(centre - osg::Vec3f(0.0f, 260.0f, 0.0f), centre, 60.0f, size, size, 10000.0f);

            std::vector<std::uint8_t> byInstance;
            std::vector<std::uint8_t> byVertex;
            const std::uint32_t instanceHits = countHits(placedByInstance, noTextures(), camera, size, byInstance);
            const std::uint32_t vertexHits = countHits(placedByVertex, noTextures(), camera, size, byVertex);

            // Both blank would agree for the wrong reason.
            ASSERT_GT(vertexHits, 0u);
            EXPECT_EQ(instanceHits, vertexHits);
            EXPECT_EQ(byInstance, byVertex);
        }

        /// A wall smaller than the frame leaves sky around it, and the count is the area it covers.
        ///
        /// The frame is 115.47 units tall at a hundred units, so a wall 60 units across covers
        /// 60 / 115.47 of the image in each direction: 0.5196 squared, which is 27.0% of 4096
        /// pixels — 1106 of them, give or take the pixels the edge falls inside.
        TEST_F(RtxVisibilityTest, aWallSmallerThanTheFrameCoversTheAreaItSubtends)
        {
            constexpr std::uint32_t size = 64;

            const std::array positions{
                osg::Vec3f(-30.0f, 0.0f, -30.0f),
                osg::Vec3f(30.0f, 0.0f, -30.0f),
                osg::Vec3f(30.0f, 0.0f, 30.0f),
                osg::Vec3f(-30.0f, 0.0f, 30.0f),
            };
            constexpr std::array<std::uint32_t, 6> indices{ 0, 1, 2, 0, 2, 3 };

            SceneDesc scene;
            const Index mesh = scene.addMesh(positions, {}, {}, indices);
            scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::identity(), .mMesh = mesh });

            const Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);

            std::vector<std::uint8_t> pixels;
            const std::uint32_t hits = countHits(scene, noTextures(), camera, size, pixels);

            const float halfExtent = 100.0f * std::tan(osg::DegreesToRadians(30.0f));
            const float covered = 30.0f / halfExtent;
            const auto expected = static_cast<std::uint32_t>(covered * covered * size * size);

            // Within a pixel of edge on each side of a 33-pixel square.
            const double tolerance = 2.0 * static_cast<double>(covered) * size + 4.0;
            EXPECT_NEAR(static_cast<double>(hits), static_cast<double>(expected), tolerance);
        }
    }
}
