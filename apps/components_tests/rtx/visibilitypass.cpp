#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
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

        /// The normal survives the acceleration structure, and points at the eye.
        ///
        /// The shading term is `0.25 + 0.75 * dot(normal, -direction)`, which is exactly one for a
        /// surface square to the ray and a quarter for one edge-on. So a wall facing the camera must
        /// shade to the same bytes as its own unshaded albedo — and would not if the normal came back
        /// rotated, mirrored, or read off the wrong vertex.
        TEST_F(RtxVisibilityTest, aWallFacingTheCameraShadesToExactlyItsAlbedo)
        {
            // Odd, so that one pixel sits exactly on the axis and its ray is exactly square to the
            // wall. At an even size the middle is half a pixel off and the term is 0.9996, which
            // rounds to a different byte and would make "exactly" a lie.
            constexpr std::uint32_t size = 33;
            const Shaders::VisibilityConstants shaded = makeCamera(
                osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);

            Shaders::VisibilityConstants albedo = shaded;
            albedo.mShowAlbedo = 1u;

            std::vector<std::uint8_t> withShading;
            std::vector<std::uint8_t> withoutShading;
            countHits(makeWall(), noTextures(), shaded, size, withShading);
            countHits(makeWall(), noTextures(), albedo, size, withoutShading);

            const std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;
            EXPECT_EQ(withShading[centre], withoutShading[centre]);
            EXPECT_EQ(withShading[centre + 1], withoutShading[centre + 1]);
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

        /// The same scene with the camera turned around. Nothing is in front of it, so nothing is hit
        /// — the check that the pass reports geometry rather than reporting that it ran.
        TEST_F(RtxVisibilityTest, aCameraFacingAwayHitsNothingAndTheImageIsAllSky)
        {
            constexpr std::uint32_t size = 64;
            const Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, -200.0f, 0.0f), 60.0f, size, size, 10000.0f);

            std::vector<std::uint8_t> pixels;
            EXPECT_EQ(countHits(makeWall(), noTextures(), camera, size, pixels), 0u);

            // The sky runs from the horizon to the zenith, so its red is somewhere in 0.10 to 0.30 and
            // never zero — and a pixel showing this wall would have exactly zero red. Asserting one
            // colour instead would be wrong: only the middle row of rays is level, and the rest tilt
            // far enough to move along the gradient.
            ASSERT_EQ(pixels.size(), std::size_t{ size } * size * 4);
            for (std::size_t i = 0; i < pixels.size(); i += 4)
            {
                ASSERT_GE(pixels[i], std::uint8_t{ 25 }) << "red at pixel " << i / 4;
                ASSERT_LE(pixels[i], std::uint8_t{ 77 }) << "red at pixel " << i / 4;
                ASSERT_GE(pixels[i + 2], std::uint8_t{ 76 }) << "blue at pixel " << i / 4;
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
