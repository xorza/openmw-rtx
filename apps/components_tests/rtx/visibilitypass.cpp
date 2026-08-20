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

#include "allocations.hpp"
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

        /// Two triangles of a quad, wound so its face points the way its corners were listed.
        constexpr std::array<std::uint32_t, 6> sQuadIndices{ 0, 1, 2, 0, 2, 3 };

        /// The unit square, in the same corner order — a texture laid once across a quad.
        const std::array<osg::Vec2f, 4> sQuadUv{
            osg::Vec2f(0.0f, 0.0f),
            osg::Vec2f(1.0f, 0.0f),
            osg::Vec2f(1.0f, 1.0f),
            osg::Vec2f(0.0f, 1.0f),
        };

        /// A level square of `extent` about the origin at height `z`, facing up.
        std::array<osg::Vec3f, 4> makeSheet(float extent, float z)
        {
            return {
                osg::Vec3f(-extent, -extent, z),
                osg::Vec3f(extent, -extent, z),
                osg::Vec3f(extent, extent, z),
                osg::Vec3f(-extent, extent, z),
            };
        }

        /// A level sheet of water `extent` across at z = 0, with nothing under it.
        SceneDesc makeOpenWater(float extent)
        {

            SceneDesc scene;
            Material water;
            water.mKind = MaterialKind::Water;
            scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::identity(),
                .mMesh = scene.addMesh(makeSheet(extent, 0.0f), {}, {}, sQuadIndices),
                .mMaterial = scene.addMaterial(water) });

            return scene;
        }

        /// A level bed `depth` units under a level surface of water, both `extent` across.
        ///
        /// The shape most of the water tests want: something to see through the water at, and the
        /// water to see it through.
        SceneDesc makeFlooded(float extent, float depth)
        {

            SceneDesc scene = makeOpenWater(extent);
            scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::identity(),
                .mMesh = scene.addMesh(makeSheet(extent, -depth), {}, {}, sQuadIndices) });

            return scene;
        }

        /// How bright the sun is in the tests that measure through water.
        ///
        /// Named because their arithmetic uses it as well: the number the shader is handed and the
        /// number an expectation is computed from have to be one number, or the test quietly stops
        /// describing the shader.
        constexpr float sSunOverWater = 2.0f;

        /// The sun's direction of travel, from how far it stands off the vertical.
        osg::Vec3f sunAt(float zenith)
        {
            return osg::Vec3f(0.0f, std::sin(zenith), -std::cos(zenith));
        }

        /// Where the sun stands over the tests that measure through water — and it must not be
        /// straight up.
        ///
        /// Overhead, the sun's own mirror image lands exactly where a camera looking straight down
        /// sends its reflection ray, and a saturated disc of sun in the middle of the frame is not
        /// what these are measuring. Two degrees puts it twelve disc-widths clear of the reflection
        /// and costs the arithmetic a part in a thousand, which `throughFlatWater` accounts for
        /// anyway.
        const float sNearlyOverhead = osg::DegreesToRadians(2.0f);

        /// Puts `camera` over a flooded scene with nothing in its sky but the sun.
        ///
        /// The water level is the one `makeFlooded` builds to, and the sky is black — so apart from
        /// the sun's own disc, the two per cent that reflects off the surface reflects nothing.
        /// Every byte in the frame has then come up through the depth, which is what lets these
        /// tests name an exact value.
        void litThroughWater(Shaders::VisibilityConstants& camera, float zenith = sNearlyOverhead)
        {
            camera.mSunDirection = sunAt(zenith);
            camera.mSunIrradiance = osg::Vec3f(sSunOverWater, sSunOverWater, sSunOverWater);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mWaterLevel = 0.0f;
        }

        /// A square in the xz plane at y = 0, facing along -Y, four hundred units across.
        SceneDesc makeWall()
        {
            const std::array positions{
                osg::Vec3f(-200.0f, 0.0f, -200.0f),
                osg::Vec3f(200.0f, 0.0f, -200.0f),
                osg::Vec3f(200.0f, 0.0f, 200.0f),
                osg::Vec3f(-200.0f, 0.0f, 200.0f),
            };

            SceneDesc scene;
            const Index mesh = scene.addMesh(positions, {}, {}, sQuadIndices);
            scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::identity(), .mMesh = mesh });
            return scene;
        }

        /// What the shader writes for a linear value, so a test can name the byte it expects.
        std::uint8_t encodeSrgb(float linear)
        {
            const float encoded
                = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            return static_cast<std::uint8_t>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
        }

        /// A byte the shader wrote, back to the linear value behind it.
        ///
        /// Ratios have to be taken in linear. sRGB is a power curve, so the same proportional
        /// brightening is a different number of bytes at the top of the range and at the bottom.
        float decodeSrgb(std::uint8_t byte)
        {
            const float encoded = static_cast<float>(byte) / 255.0f;
            return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }

        /// What the centre pixel must read over `depth` units of flat water with a bed under it.
        ///
        /// The bed is untextured, so its albedo is 0.5, and **the water is crossed twice**: the sun
        /// is attenuated on its way down and again on the way back up to the eye, so what arrives is
        /// the product of two paths and not one of them. Lighting the bottom as though the water
        /// over it were not there is what makes the same column read differently from above and
        /// below.
        ///
        /// The way down is the *slant* path — Snell bends the sun to `asin(sin(zenith) / IOR)` and
        /// it crosses `depth / cos` of water — while the way back is vertical, because the camera
        /// is. The cosine on the bed stays the sun's own in air: refraction at a level surface moves
        /// no flux across a horizontal patch, so what lands on the bottom is what fell on the top
        /// times whatever the longer path took.
        ///
        /// Nothing else adds. The scattering term `(1 - T^2) / 2` meets a black ambient, and the
        /// sky is black, so all the surface reflects is the two per cent it takes off the way in.
        int throughFlatWater(float depth, float zenith, std::size_t channel)
        {
            const float sine = std::sin(zenith) / Shaders::WATER_IOR;
            const float refracted = std::sqrt(1.0f - sine * sine);

            const float down = std::exp(-Shaders::WATER_EXTINCTION[channel] * depth / refracted);
            const float up = std::exp(-Shaders::WATER_EXTINCTION[channel] * depth);
            const float bed = 0.5f * sSunOverWater * std::cos(zenith) * Shaders::INV_PI;

            return encodeSrgb(bed * down * up * (1.0f - Shaders::WATER_F0));
        }

        /// How brightly the sky is lit, and what colour the air is, in the tests that measure a
        /// wall through fog.
        ///
        /// Named for the reason `sSunOverWater` is: each test's arithmetic uses these as well as
        /// handing them to the shader, and the two have to be one number. The haze is deliberately
        /// not grey, so a fog scattering the wrong colour cannot pass by matching a total.
        ///
        /// **The sky rather than the cell's ambient, because a wall is lit by what it can see.** The
        /// ambient is what terminates a path now, one bounce further along; what fills a surface the
        /// eye is looking at is the hemisphere it gathers. A sky of one radiance makes that gather
        /// exact rather than noisy — every direction returns the same number, so one sample is the
        /// whole answer.
        constexpr float sFoggySky = 0.6f;
        const osg::Vec3f sHaze(0.1f, 0.2f, 0.4f);

        /// Puts `camera` under a sky of one radiance and nothing else, with air of `extinction` in it
        /// pooling at `level`.
        void litThroughFog(Shaders::VisibilityConstants& camera, float extinction,
            float level = -std::numeric_limits<float>::infinity())
        {
            camera.mSkyHorizon = osg::Vec3f(sFoggySky, sFoggySky, sFoggySky);
            camera.mSkyZenith = camera.mSkyHorizon;
            camera.mFogColour = sHaze;
            camera.mFogExtinction = extinction;
            camera.mWaterLevel = level;

            // Even air, which is what every exact expectation here needs: a banked field varies
            // along the ray, and then the march stops telescoping and its answer stops being one
            // anyone can write down. The banks have their own test.
            camera.mFogUniform = 1.0f;
        }

        /// A texture whose every mip is one flat colour — level `i` is `40 + 30i`, evenly spaced
        /// and none of them black.
        ///
        /// **The byte a ray comes back with reads out the level it sampled**, and because the levels
        /// are evenly spaced in value, `textureLod` blending two of them lands exactly on
        /// `40 + 30 * lod`. A *fractional* level is readable that way, which is what makes a cone's
        /// width measurable rather than merely orderable. Flat colours also mean the answer does not
        /// depend on where in the texture the cone landed.
        TextureArray makeMipLadder(Device& device, CommandPool& pool)
        {
            constexpr std::uint32_t extent = 64;
            constexpr std::uint32_t levels = 7;

            std::vector<std::uint8_t> bytes;
            std::vector<MipLevel> mips;
            for (std::uint32_t level = 0; level < levels; ++level)
            {
                const std::uint32_t side = extent >> level;
                mips.push_back(MipLevel{ static_cast<std::uint32_t>(bytes.size()), side, side });
                bytes.insert(bytes.end(), std::size_t{ side } * side * 4, static_cast<std::uint8_t>(40 + 30 * level));
            }

            const TextureData data{
                .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                .mWidth = extent,
                .mHeight = extent,
                .mBytes = std::as_bytes(std::span(bytes)),
                .mLevels = mips,
            };

            std::vector<Texture> uploaded;
            uploaded.emplace_back(device, pool, data, "mip ladder");
            return TextureArray(device, std::move(uploaded));
        }

        /// Which level of `makeMipLadder` a linear sample came from.
        float ladderLevel(float sampled)
        {
            return (sampled * 255.0f - 40.0f) / 30.0f;
        }

        /// The lobe the shader arrives at for a sea whose every wave is past the cone's reach.
        ///
        /// Read off the same table the shader reads. With no wave resolved, `unresolved` is the
        /// whole spectrum's mean square slope, and the lobe is twice its root — a normal tilted by
        /// an angle turns a reflection by twice it.
        float lobeOf(const SeaState& sea)
        {
            float unresolved = 0.0f;
            for (const Shaders::GpuWave& wave : sea.getWaves())
            {
                const float steepness = wave.mAmplitude * wave.mWavenumber;
                unresolved += 0.5f * steepness * steepness;
            }

            return std::min(2.0f * std::sqrt(unresolved), 1.0f);
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

            /// Renders `scene` at `size` square and returns how many primary rays hit.
            /// @param sea what the water is doing. A state with no height in it is a flat sea, which
            ///        is what a test asserting an exact transmittance through one needs.
            /// @param accumulate how many differently-seeded frames to average, overriding the
            ///        camera's own frame index with 0, 1, ... Zero renders the camera's frame alone.
            ///        Every frame hits the same primary geometry, so the count returned is divided
            ///        back down rather than reported as however many frames were traced.
            std::uint32_t countHits(const SceneDesc& scene, const TextureArray& textures,
                const Shaders::VisibilityConstants& camera, std::uint32_t size, std::vector<std::uint8_t>& pixels,
                const SeaState& sea = SeaState{}, std::uint32_t accumulate = 0)
            {
                Device& device = *mHarness->mDevice;
                CommandPool pool(device);
                const SceneAcceleration acceleration(device, pool, scene);
                const SceneBuffers buffers(device, pool, scene, acceleration.getIndices(), sea);

                const VisibilityPass& pass = passFor(textures.getLayout());
                const VisibilityInputs inputs{
                    .mScene = acceleration.getTopLevel(),
                    .mBuffers = &buffers,
                    .mTextures = textures.getSet(),
                };

                Image target(device, size, size, VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
                Image history(device, size, size, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT);

                const Buffer hits(device, sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                *static_cast<std::uint32_t*>(hits.map()) = 0;
                hits.unmap();

                const std::uint32_t frames = std::max(accumulate, 1u);

                pool.submitAndWait([&](VkCommandBuffer commands) {
                    target.transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                    history.transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

                    for (std::uint32_t frame = 0; frame < frames; ++frame)
                    {
                        // Each dispatch reads the sum the one before it wrote, and overwrites the
                        // same display image. One submit rather than several, so the barrier here is
                        // what orders them rather than a queue that happened to drain.
                        if (frame > 0)
                        {
                            const auto both
                                = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                            history.transition(commands, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, both);
                            target.transition(commands, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                        }

                        Shaders::VisibilityConstants sampled = camera;
                        if (accumulate > 0)
                        {
                            sampled.mFrame = frame;
                            sampled.mAccumulate = frame + 1;
                        }

                        pass.record(commands, inputs, target, history, hits, sampled);
                    }
                });

                pixels = target.read(pool, VK_IMAGE_LAYOUT_GENERAL);

                const std::uint32_t count = *static_cast<const std::uint32_t*>(hits.map());
                hits.unmap();
                return count / frames;
            }

            /// The pass, kept between renders because building one compiles the shader.
            ///
            /// **Half a second a render, measured.** The driver turns the module's SPIR-V into
            /// machine code when the pipeline is created, and this one is a ray-query shader that
            /// traces twice and inlines everything it shades with — so a test that rendered three
            /// times spent a second and a half compiling the same program three times. Nothing about
            /// a pass depends on the scene; only on the device and on the shape of the texture set.
            ///
            /// Rebuilt when that shape changes, which in practice is once: a test uses one texture
            /// array throughout, and most use the empty one.
            const VisibilityPass& passFor(VkDescriptorSetLayout textures)
            {
                if (mPass == nullptr || mPassLayout != textures)
                {
                    mPass
                        = std::make_unique<VisibilityPass>(*mHarness->mDevice, Testing::getShaderDirectory(), textures);
                    mPassLayout = textures;
                }

                return *mPass;
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
            std::unique_ptr<VisibilityPass> mPass;
            VkDescriptorSetLayout mPassLayout = VK_NULL_HANDLE;
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
            constexpr float halfExtent = 57.735027f;

            Device& device = *mHarness->mDevice;
            CommandPool pool(device);
            const TextureArray textures = makeMipLadder(device, pool);

            const std::array positions{
                osg::Vec3f(-halfExtent, 0.0f, -halfExtent),
                osg::Vec3f(halfExtent, 0.0f, -halfExtent),
                osg::Vec3f(halfExtent, 0.0f, halfExtent),
                osg::Vec3f(-halfExtent, 0.0f, halfExtent),
            };

            SceneDesc scene;
            const Index mesh = scene.addMesh(positions, {}, sQuadUv, sQuadIndices);
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
            constexpr std::array<float, 2> firstMask{ 1.0f, 0.0f };
            constexpr std::array<float, 2> secondMask{ 0.0f, 1.0f };

            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);
            camera.mShowAlbedo = 1u;

            /// @param second the texture slot and diffuse transform of the layer on the right.
            const auto render = [&](Index second, const osg::Vec4f& secondTransform) {
                SceneDesc scene;
                const Index mesh = scene.addMesh(positions, {}, sQuadUv, sQuadIndices);
                scene.addTexture(VFS::Path::NormalizedView("red.dds"));
                scene.addTexture(VFS::Path::NormalizedView("green.dds"));
                scene.addTexture(VFS::Path::NormalizedView("strip.dds"));

                Material material;
                material.mKind = MaterialKind::Terrain;
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

            // **A sky rather than the cell's ambient, because that is what fills a wall now.** The
            // ambient terminates a path one bounce further along; what a surface the eye can see
            // gathers is its own hemisphere, and a sky of one radiance makes that gather exact —
            // every direction returns the same number, so one sample is the whole answer.
            const auto render = [&](const osg::Vec3f& direction, const osg::Vec3f& irradiance, bool blocked,
                                    const osg::Vec3f& sky = osg::Vec3f()) {
                SceneDesc scene = makeWall();
                if (blocked)
                    scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::identity(),
                        .mMesh = scene.addMesh(occluder, {}, {}, sQuadIndices) });

                Shaders::VisibilityConstants camera = base;
                camera.mSunDirection = direction;
                camera.mSunIrradiance = irradiance;
                camera.mSkyHorizon = sky;
                camera.mSkyZenith = sky;

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
            // rather than arithmetically applied. Asserted against the sky, because a negative
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

            // Nothing lights this scene at all: no lamp, no sun, no ambient. Whatever comes back is
            // the surface's own glow and nothing else.
            const Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);

            const auto render = [&](Index diffuse, Index emissiveMap, const osg::Vec3f& emissiveColour) {
                SceneDesc scene;
                const Index mesh = scene.addMesh(positions, {}, sQuadUv, sQuadIndices);
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

            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -150.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);
            camera.mShowAlbedo = 1u;

            const auto render = [&](AlphaMode mode, float alphaRef) {
                SceneDesc scene = makeWall();
                const Index mesh = scene.addMesh(masked, {}, sQuadUv, sQuadIndices);
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

            // A sky rather than the cell's ambient, for the reason the sun's own test gives: what
            // fills a wall the eye can see is the hemisphere it gathers.
            const auto render = [&](const std::optional<Light>& light, const osg::Vec3f& sky, bool blocked) {
                SceneDesc scene = makeWall();
                if (light.has_value())
                    scene.addLight(*light);
                if (blocked)
                    scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::identity(),
                        .mMesh = scene.addMesh(occluder, {}, {}, sQuadIndices) });

                Shaders::VisibilityConstants camera = base;
                camera.mSkyHorizon = sky;
                camera.mSkyZenith = sky;

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
            const osg::Vec3f sky(0.4f, 0.4f, 0.4f);
            EXPECT_EQ(render(std::nullopt, sky, false), 124) << "the sky alone";

            // A lamp behind the wall meets it at a cosine of minus one, and is dropped rather than
            // arithmetically applied. Asserted against the sky and not against black, because
            // black is what a negative contribution clamps to as well — the two only tell apart
            // where there is something for it to be subtracted from.
            Light behind = lamp;
            behind.mPosition = osg::Vec3f(0.0f, 50.0f, 0.0f);
            EXPECT_EQ(render(behind, sky, false), 124) << "a lamp on the far side lights nothing";

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

        /// A bounce is drawn by the cosine, and two thirds is the number that says so.
        ///
        /// **The one property of the estimator a uniform sky cannot show.** Every other test here
        /// fills the sky with one radiance so that a single sample carries no variance — which is
        /// what makes them exact, and what leaves `cosineDirection` unmeasured. A sky that runs from
        /// horizon to zenith turns the direction itself into the answer.
        ///
        /// Malley's method draws `d.z = sqrt(1 - u)` for uniform `u`, so
        ///
        ///   E[d.z]   = integral of sqrt(t) over [0, 1]  = 2/3
        ///   Var[d.z] = E[1 - u] - (2/3)^2 = 1/2 - 4/9   = 1/18,  sd = 0.235702
        ///
        /// A floor of albedo 0.5 under `mix(horizon, zenith, d.z)` therefore has to come back at
        /// `0.5 * (horizon + 2/3 * range)` per channel, spread by `0.5 * |range| * 0.235702`.
        ///
        /// **The mean is also what tells this estimator from the wrong one.** Drawing uniformly over
        /// the hemisphere and carrying the cosine as a weight is unbiased as well, but its
        /// directions average `E[d.z] = 1/2` — a picture a sixth of the sky's range away, which is
        /// ten times the tolerance below and cannot be mistaken for it.
        TEST_F(RtxVisibilityTest, aBounceDrawsItsDirectionByTheCosineAndNotUniformly)
        {
            constexpr std::uint32_t size = 64;
            constexpr float samples = float{ size } * size;

            SceneDesc scene;
            scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::identity(),
                .mMesh = scene.addMesh(makeSheet(4000.0f, 0.0f), {}, {}, sQuadIndices) });

            // Three ranges rather than one, and one of them descending, so a test that passed by
            // matching a total or by swapping two channels would not.
            const osg::Vec3f horizon(0.20f, 0.15f, 0.60f);
            const osg::Vec3f zenith(0.80f, 0.65f, 0.15f);

            // As near straight down as `makeCamera` will take, so every ray lands on the floor and
            // every pixel is one bounce off a normal of +z with nothing else in the frame. Nothing
            // stands above the floor either, so every bounce escapes and the sky is the whole answer.
            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -1.0f, 300.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = horizon;
            camera.mSkyZenith = zenith;

            const auto shade = [&](std::uint32_t frame, std::uint32_t accumulate = 0) {
                camera.mFrame = frame;

                std::vector<std::uint8_t> pixels;
                EXPECT_EQ(countHits(scene, noTextures(), camera, size, pixels, SeaState{}, accumulate), size * size);
                return pixels;
            };

            // The mean and the standard deviation of one channel across the frame, in linear.
            const auto measure = [&](const std::vector<std::uint8_t>& pixels, std::size_t channel) {
                float sum = 0.0f;
                float squares = 0.0f;
                for (std::size_t i = channel; i < pixels.size(); i += 4)
                {
                    const float linear = decodeSrgb(pixels[i]);
                    sum += linear;
                    squares += linear * linear;
                }

                const float mean = sum / samples;
                return std::pair{ mean, std::sqrt(squares / samples - mean * mean) };
            };

            const std::vector<std::uint8_t> first = shade(0);
            ASSERT_EQ(first.size(), std::size_t{ size } * size * 4);

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto [mean, spread] = measure(first, channel);
                const float range = zenith[channel] - horizon[channel];
                const float byTheCosine = 0.5f * (horizon[channel] + range * 2.0f / 3.0f);
                const float ifDrawnEvenly = 0.5f * (horizon[channel] + range * 0.5f);

                // One sRGB step at a quarter brightness is 0.004 of linear — a 255th divided by the
                // curve's slope there — and it swamps the sampling standard error, which over 4096
                // samples is `0.5 * |range| * 0.235702 / 64`, at most 0.0011.
                EXPECT_NEAR(mean, byTheCosine, 0.004f) << "channel " << channel << " averages two thirds up";
                EXPECT_GT(std::abs(mean - ifDrawnEvenly), 0.02f)
                    << "channel " << channel << " is nowhere near the half an even draw would give";

                EXPECT_NEAR(spread, 0.5f * std::abs(range) * 0.235702f, 0.003f)
                    << "channel " << channel << " is spread by the square root's own variance";
            }

            // The frame index has to move every pixel's draw, or a bounce would be a fixed pattern
            // that no amount of accumulation could average away. Two independent samples land on the
            // same byte only by coincidence — green's range covers some eighty of them here, so a
            // few per cent — and the rest must differ.
            const std::vector<std::uint8_t> second = shade(1);
            std::size_t moved = 0;
            for (std::size_t i = 1; i < first.size(); i += 4)
                moved += first[i] != second[i] ? 1u : 0u;

            EXPECT_GT(moved, std::size_t{ size } * size * 9 / 10) << "the frame redraws the bounce";

            // **And what the accumulator is for: the error falls as the square root of the count.**
            // Averaging sixty-four independent draws divides the standard deviation of each by eight
            // and leaves the mean where it was — the whole basis for calling a long run a reference,
            // and worth asserting rather than assuming, because a sum that dropped or double-counted
            // a frame would still look converged.
            //
            // Green: 0.058926 spread over one draw, so 0.007366 over sixty-four. The spread's
            // tolerance is a fifth of that, which the estimate's own uncertainty — a standard
            // deviation over `sqrt(2 * 4096)` samples, about 1% — sits well inside.
            //
            // **The mean's tolerance is twenty times tighter than the single-frame one above**, and
            // has to be: at sixty-four samples a pixel's values cluster inside a few bytes, so the
            // sRGB step that dominated there is no longer what limits this. A divisor off by one
            // moves the mean by 0.0046 — the whole point of the assertion, and something a tolerance
            // sized for one noisy frame would wave through.
            constexpr std::uint32_t averaged = 64;
            const std::vector<std::uint8_t> converged = shade(0, averaged);

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto [mean, spread] = measure(converged, channel);
                const float range = zenith[channel] - horizon[channel];

                EXPECT_NEAR(mean, 0.5f * (horizon[channel] + range * 2.0f / 3.0f), 0.0002f)
                    << "channel " << channel << " keeps its mean when averaged";

                const float alone = 0.5f * std::abs(range) * 0.235702f;
                EXPECT_NEAR(spread, alone / std::sqrt(float{ averaged }), 0.2f * alone / std::sqrt(float{ averaged }))
                    << "channel " << channel << " converges as the square root of the count";
            }
        }

        /// Water is seen by a camera and not by a shadow ray, and the mask is what says so.
        ///
        /// Sunlight reaching a seabed has come through the surface, so a sea that occluded would
        /// black out every shallow in the game. Telling traversal in the mask costs nothing; the
        /// alternative — non-opaque water and a candidate loop that waves shadow rays past — was
        /// measured at half the frame rate, because every shadow ray crossing the sea then invokes a
        /// shader where traversal alone had been enough.
        TEST_F(RtxVisibilityTest, waterIsVisibleToACameraAndInvisibleToAShadowRay)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;

            // A pane twenty units across at y = -50, between the wall and everything in front of it.
            const std::array pane{
                osg::Vec3f(-20.0f, -50.0f, -20.0f),
                osg::Vec3f(20.0f, -50.0f, -20.0f),
                osg::Vec3f(20.0f, -50.0f, 20.0f),
                osg::Vec3f(-20.0f, -50.0f, 20.0f),
            };

            const auto renderPixel = [&](MaterialKind kind, bool lookAtIt) {
                SceneDesc scene = makeWall();

                Material material;
                material.mKind = kind;
                scene.addInstance(MeshInstance{
                    .mTransform = osg::Matrixf::identity(),
                    .mMesh = scene.addMesh(pane, {}, {}, sQuadIndices),
                    .mMaterial = scene.addMaterial(material),
                });

                // Off the sun's axis, so the centre pixel lands on the patch of wall the pane
                // shadows without the camera's own ray having to cross the pane: it passes y = -50
                // at x = 50, and the pane reaches 20. Or straight at the pane, to see it at all.
                Shaders::VisibilityConstants camera = lookAtIt
                    ? makeCamera(
                          osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, -50.0f, 0.0f), 60.0f, size, size, 10000.0f)
                    : makeCamera(
                          osg::Vec3f(100.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);
                camera.mSunDirection = osg::Vec3f(0.0f, 1.0f, 0.0f);
                camera.mSunIrradiance = osg::Vec3f(2.0f, 2.0f, 2.0f);

                std::vector<std::uint8_t> pixels;
                EXPECT_GT(countHits(scene, noTextures(), camera, size, pixels), 0u);
                return std::array<std::uint8_t, 3>{ pixels[centre], pixels[centre + 1], pixels[centre + 2] };
            };
            const auto render = [&](MaterialKind kind, bool lookAtIt) { return renderPixel(kind, lookAtIt)[0]; };

            // A solid pane stops the camera's ray and the sun's alike, so the wall behind is dark.
            EXPECT_EQ(render(MaterialKind::Surface, false), 0) << "a solid pane shadows the wall";

            // The same pane as water: the camera still meets it, and the sun goes straight through.
            // 0.5 albedo times 2.0 over pi is 0.318310, which encodes to 153 of 255.
            EXPECT_EQ(render(MaterialKind::Water, false), 153) << "and water does not";

            // And the camera does still meet it. Asserted as "bluer than it is red", which the wall
            // cannot be at any brightness — it is grey through every channel — and which survives
            // water's shading changing, as it will.
            const std::array<std::uint8_t, 3> seen = renderPixel(MaterialKind::Water, true);
            EXPECT_GT(seen[2], seen[0]) << "though a camera still sees it";
        }

        /// Water over a bed, absorbing exactly what Beer-Lambert says it should over the path taken.
        ///
        /// A flat sea, so the surface is its own plane and the ray crosses the depth once rather than
        /// through whatever facet a wave put in the way. Nearly straight down — `makeCamera` will not
        /// take a look along the world's up axis, so this is a fifth of a degree off it, where the
        /// cosine is 0.99999 and Fresnel is its head-on 0.02.
        ///
        /// **The sun's path is not the depth unless the sun is overhead**, which is the second half
        /// of this: it enters at Snell's angle and crosses more water than it would straight down.
        ///
        /// Every expectation here derives from `WATER_EXTINCTION`, so a tuning pass is one line
        /// rather than five pieces of arithmetic that quietly stop describing the shader.
        TEST_F(RtxVisibilityTest, waterTakesWhatBeerLambertSaysOverThePathTheLightTook)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;
            constexpr float depth = 200.0f;

            // A bed and a surface, both level and both wide enough to fill the frame.
            const SceneDesc scene = makeFlooded(400.0f, depth);

            const auto look = [&](float zenith) {
                Shaders::VisibilityConstants camera = makeCamera(
                    osg::Vec3f(0.0f, -1.0f, 400.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);
                litThroughWater(camera, zenith);

                // No height at all, which is a flat sea: a table whose amplitudes are zero. It is
                // also what makes the caustic exactly one — a flat surface has no curvature to
                // gather anything with, so the Jacobian is the identity.
                std::vector<std::uint8_t> pixels;
                countHits(scene, noTextures(), camera, size, pixels, SeaState{ .mSignificantHeight = 0.0f });
                return std::array<int, 3>{ pixels[centre], pixels[centre + 1], pixels[centre + 2] };
            };

            // A sun as near overhead as the reflection allows, where `throughFlatWater`'s slant is
            // a part in three thousand and the answer is all but exactly `bed * T^2`:
            //
            //   0.5 * 2.0 / pi = 0.318310, times the two transmittances, times the 0.98 that is not
            //   the surface's own reflection.
            const std::array<int, 3> overhead = look(sNearlyOverhead);
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(overhead[channel], throughFlatWater(depth, sNearlyOverhead, channel), 1)
                    << "channel " << channel << " under a sun all but overhead";

            // And the ordering that makes this water's colour. Red goes first — every water
            // absorbs it within a metre or two, which is the one thing about water's colour that is
            // not a matter of taste. **Green survives most, not blue**, which is what makes a
            // coastal shelf green where open ocean is blue: Jerlov's coastal extinction is
            // 0.004572, 0.000714, 0.001143, so blue is taken half again as fast as green.
            EXPECT_LT(overhead[0], overhead[2]) << "red is taken before blue";
            EXPECT_LT(overhead[2], overhead[1]) << "and blue before green";

            // And now 45 degrees off the vertical, where the slant is the whole point. Snell's law
            // turns the sun to `asin(sin(45) / 1.333)` = 32.03 degrees, so it reaches a bed 200
            // units down after 200 / cos(32.03) = 235.93 units of water rather than 200 — while the
            // view back up is unchanged. One expectation serves both angles, which is what makes
            // this a measurement of the zenith rather than two unrelated numbers.
            const float slanted = osg::DegreesToRadians(45.0f);
            const std::array<int, 3> across = look(slanted);
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(across[channel], throughFlatWater(depth, slanted, channel), 1)
                    << "channel " << channel << " under a slanted sun";

            // The parameter has to matter, and it does in the direction physics says: a slanted sun
            // both meets the bed at a cosine and crosses more water to get there, so less of it
            // comes back. Red, which the extra 36 units of water costs the most.
            EXPECT_LT(across[0], overhead[0]) << "a slanted sun reaches the bed with less left";
        }

        /// Deep water settles at what it scatters, and at half what only-the-return-leg would give.
        ///
        /// Light scattered toward the eye had to get down there first. Attenuating only the way back
        /// lets deep water asymptote to the scattering colour at full sky brightness, which is a
        /// milky sheet rather than a channel. Integrating both legs replaces `1 - T` with
        /// `(1 - T^2) / 2` — the same answer in the shallows, half as bright where it settles.
        ///
        /// Two thousand units down, red's transmittance is `exp(-9.144)`, a part in ten thousand, so
        /// what comes back is the scattering term almost alone:
        ///
        ///   0.5 * 1.07e-4 + 0.012 * 0.5 * 1.0 = 0.0060535
        ///   times the 0.98 that is not Fresnel  = 0.0059324
        ///   1.055 * 0.0059324^(1/2.4) - 0.055   = 0.06958, or 18 of 255
        ///
        /// Only the return leg would put the same pixel at 28 — the factor of two, made visible.
        TEST_F(RtxVisibilityTest, deepWaterSettlesAtHalfWhatOneAttenuatedLegWouldGive)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;
            constexpr float depth = 2000.0f;

            const SceneDesc scene = makeFlooded(4000.0f, depth);

            // No sun and a black sky, so the ambient is the only light and the two per cent that
            // reflects off the surface reflects nothing.
            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -1.0f, 400.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mAmbient = osg::Vec3f(1.0f, 1.0f, 1.0f);

            // No height at all, which is a flat sea: a table whose amplitudes are zero.
            std::vector<std::uint8_t> pixels;
            countHits(scene, noTextures(), camera, size, pixels, SeaState{ .mSignificantHeight = 0.0f });

            EXPECT_EQ(pixels[centre], 18) << "red, settled at what the water scatters";
        }

        /// The same column of water has to look the same from either side of it.
        ///
        /// **This is the test that found the missing half.** Seen from ten units above, a ray crosses
        /// the surface, the water and back; seen from ten units below, it crosses the water and the
        /// ray is fogged by it directly. Those are different code paths through the same physics, and
        /// they have to agree — which they cannot while the light reaching the bottom is not
        /// attenuated by the water over it.
        ///
        /// M6's stated done-when, and the sort of thing that is found by measuring rather than by
        /// looking: neither view is obviously wrong on its own.
        TEST_F(RtxVisibilityTest, aColumnOfWaterAgreesFromAboveAndFromBelow)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;
            constexpr float depth = 200.0f;

            const SceneDesc scene = makeFlooded(400.0f, depth);

            // A fifth of a degree off the vertical, which `makeCamera` insists on and which changes
            // the path by a part in ten thousand.
            const auto look = [&](float from) {
                Shaders::VisibilityConstants camera = makeCamera(
                    osg::Vec3f(0.0f, -0.05f, from), osg::Vec3f(0.0f, 0.0f, from - 10.0f), 60.0f, size, size, 10000.0f);
                litThroughWater(camera);

                std::vector<std::uint8_t> pixels;
                countHits(scene, noTextures(), camera, size, pixels, SeaState{ .mSignificantHeight = 0.0f });
                return std::array<int, 3>{ pixels[centre], pixels[centre + 1], pixels[centre + 2] };
            };

            // Ten above and ten below, and the two legitimately differ by a little. From above the
            // ray crosses the whole 200 units after the surface and loses two per cent to Fresnel on
            // the way in; from below it crosses 190 and meets no surface at all. In radiance that is
            //
            //   exp(0.004572 * 10) / 0.98 = 1.068
            //
            // for red, which the sRGB curve compresses to about three per cent of a byte — so 63 and
            // 65, and a tolerance of two rather than a round fraction chosen to pass.
            const std::array<int, 3> above = look(10.0f);
            const std::array<int, 3> below = look(-10.0f);

            EXPECT_GT(above[0], 0) << "the bed is visible from above";
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_NEAR(below[channel], above[channel], 2) << "channel " << channel << ": " << above[channel]
                                                               << " from above, " << below[channel] << " from below";
        }

        /// The sky loses the column of water over a bed just as the sun does.
        ///
        /// **The half a bounce could quietly skip.** A bounce that escapes to the sky is the sky
        /// arriving at the point it left, so it crosses the same depth the sun crosses and has to
        /// lose the same fraction to it. Returning the sky whole lights a submerged floor as though
        /// the water above it were not there — the fault `aColumnOfWaterAgreesFromAboveAndFromBelow`
        /// was written for, in the one term that test cannot see: a bounce shades the same bed
        /// identically from either side of the surface, so the two views agree while both are wrong.
        ///
        /// **From under the water, because only a primary hit bounces.** A bed seen down through the
        /// surface is the far end of `waterRay`, which terminates it with `pathEnd` — the flat
        /// ambient — precisely so that a reflection cannot recurse. Put the eye below and the bed is
        /// what the camera ray found, and it gathers a real hemisphere.
        ///
        /// **A sky of one radiance, which makes that hemisphere exact rather than noisy.** Every
        /// direction returns the same number, so the single sample carries no variance and what
        /// follows is an equality rather than an average.
        ///
        /// Red, off a bed two hundred units down, seen from a hundred and ninety units above it:
        ///
        ///   down  = exp(-0.004572 * 200)         = 0.400757   the sky's own way in
        ///   bed   = 0.5 albedo * 0.6 sky * down  = 0.120227   what leaves it
        ///   out   = exp(-0.004572 * 190)         = 0.419505   and the way back to the eye
        ///   pixel = bed * out                    = 0.050436,  or 63 of 255
        ///
        /// No Fresnel and no surface: the ray never crosses one. Green and blue absorb far less and
        /// land at 131 and 121. Drop the column and red reads 99 — half the scale away, so this
        /// cannot be passed by widening a tolerance.
        TEST_F(RtxVisibilityTest, aBounceIntoTheSkyLosesTheWaterOverTheBedItLeft)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;
            constexpr float depth = 200.0f;
            constexpr float above = 190.0f;
            constexpr float sky = 0.6f;

            const SceneDesc scene = makeFlooded(4000.0f, depth);

            // Straight down but for the third of a degree `makeCamera` insists on — it refuses a
            // view along the world's up axis, where roll has no answer — so the way out is the
            // vertical column to a part in seventy thousand and the bed is met square on.
            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -1.0f, above - depth), osg::Vec3f(0.0f, 0.0f, -depth), 60.0f, size, size, 10000.0f);
            camera.mSkyHorizon = osg::Vec3f(sky, sky, sky);
            camera.mSkyZenith = camera.mSkyHorizon;
            camera.mWaterLevel = 0.0f;

            // Flat, so the surface the bounce passes through neither bends it nor gathers it.
            std::vector<std::uint8_t> pixels;
            countHits(scene, noTextures(), camera, size, pixels, SeaState{ .mSignificantHeight = 0.0f });

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const float down = std::exp(-Shaders::WATER_EXTINCTION[channel] * depth);
                const float out = std::exp(-Shaders::WATER_EXTINCTION[channel] * above);

                EXPECT_NEAR(pixels[centre + channel], int{ encodeSrgb(0.5f * sky * down * out) }, 1)
                    << "channel " << channel;
            }
        }

        /// The waves gather the sun into moving lines on the bed, and move light rather than make it.
        ///
        /// Caustics are ray density — the determinant of the Jacobian of the map from where light
        /// met the surface to where it landed — and what isolates that term from everything else in
        /// the frame is a **ratio**: the same bed, camera and sun, rendered with a sea state and
        /// without one. Dividing the two cancels the albedo, the absorption, the cosine and the
        /// geometry, and leaves the term itself, pixel for pixel.
        ///
        /// **Looked at from underneath**, so no wavy surface stands between the eye and the bed.
        /// Seen from above, what the waves do to the *view* would be mixed into what they do to the
        /// *light*, and the ratio would measure both.
        TEST_F(RtxVisibilityTest, theWavesGatherSunlightOntoTheBedWithoutMakingAnyOfIt)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::size_t count = std::size_t{ size } * size;

            // A wide look from 135 units above the bed *whatever the depth is*, so that two depths
            // see the same patch of water and their caustics can be compared pixel for pixel.
            const auto render = [&](float depth, const SeaState& sea, float seconds) {
                const SceneDesc scene = makeFlooded(4000.0f, depth);

                Shaders::VisibilityConstants camera = makeCamera(osg::Vec3f(0.0f, -1.0f, 135.0f - depth),
                    osg::Vec3f(0.0f, 0.0f, -depth), 90.0f, size, size, 100000.0f);
                litThroughWater(camera);
                camera.mTime = seconds;

                std::vector<std::uint8_t> image;
                countHits(scene, noTextures(), camera, size, image, sea);
                return image;
            };

            // Green, which survives the water best of the three and so has the most of a byte left
            // to vary over.
            const auto causticField = [&](float depth, float seconds = 0.0f) {
                // The still sea does not move, so one baseline serves whatever the clock says.
                const std::vector<std::uint8_t> still = render(depth, SeaState{ .mSignificantHeight = 0.0f }, 0.0f);
                const std::vector<std::uint8_t> running = render(depth, SeaState{}, seconds);

                std::vector<float> field;
                field.reserve(count);
                for (std::size_t i = 0; i < count; ++i)
                    field.push_back(decodeSrgb(running[i * 4 + 1]) / decodeSrgb(still[i * 4 + 1]));

                return field;
            };

            // A hundred and forty units down, which is where the lens stops sharpening.
            const std::vector<float> capped = causticField(140.0f);
            const auto [dimmest, brightest] = std::minmax_element(capped.begin(), capped.end());

            float total = 0.0f;
            for (const float gathered : capped)
                total += gathered;
            const float mean = total / static_cast<float>(count);

            // Measured over this patch: the brightest place on the bed is gathered to 2.75 of what
            // a flat sea would put there and the dimmest thinned to 0.38, so the pattern is bold
            // rather than a wobble.
            EXPECT_GT(*brightest, 2.0f) << "measured 2.75, gathered into lines";
            EXPECT_LT(*dimmest, 0.6f) << "measured 0.38, and thinned between them";

            // **And the mean is one**, which is the claim that makes it light and not decoration.
            // It comes out at 1.024: a reciprocal of something that fluctuates is worth more than
            // the reciprocal of its mean, and what the shader takes out analytically is the second
            // order of that. **With the correction removed this reads 1.123** — four times the
            // slack allowed here, which is what makes this tolerance a test rather than a comment.
            EXPECT_NEAR(mean, 1.0f, 0.04f) << "the waves redistribute the sun, they do not make any";

            // Past the cap the lens is still the one at a hundred and forty, so a bed four hundred
            // units down gets the *same* pattern rather than a sharper one. Both are looked at from
            // the same height above the bed, so the two frames cover the same water at the same cone
            // width and the fields subtract.
            //
            // What is left is the eight-bit round trip. The deeper bed is darker — green comes back
            // at 119 of 255 against 141 — so half a byte on each of the two reads is about 1.4 per
            // cent of a ratio, and on a pixel gathered to 2.7 that is 0.04. The worst over the frame
            // measures 0.050, and the tolerance is twice it.
            //
            // Without the cap the reference renderer measured three quarters more light than fell
            // at this depth — the fold the small-angle map cannot describe.
            const std::vector<float> deeper = causticField(400.0f);
            for (std::size_t i = 0; i < count; ++i)
                ASSERT_NEAR(deeper[i], capped[i], 0.1f) << "pixel " << i << ", past the depth cap";

            // **How bold the pattern is, and how fast it moves** — M6 asks for both measured rather
            // than eyeballed, and they are the two halves of one choice. The spectrum's short cutoff
            // is a limit in *time*, not in space: the waves that focus hardest are the shortest, and
            // a wave's period falls with its length, so the same waves that make the boldest
            // caustics are the ones that make them tear.
            float spread = 0.0f;
            for (const float gathered : capped)
                spread += (gathered - mean) * (gathered - mean);

            EXPECT_NEAR(std::sqrt(spread / static_cast<float>(count)) / mean, 0.366f, 0.02f)
                << "the pattern's contrast, as a fraction of its own mean";

            // A twelfth of a second, which is how long a frame is worth caring about. For two
            // samples of one field, `E[(b - a)^2] = 2 sigma^2 (1 - rho)`, so half the ratio of the
            // two sums is the share of the pattern that is new — 51.1% here.
            //
            // That is the number the shortest wave was chosen on. The reference renderer's sweep:
            // **18 units gives the best caustics it ever drew and they tear at 73%**, 32 units comes
            // out at 51%, and 50 units is dull at 33%. This fork cuts at 32 and lands on 51 — the
            // same spectrum reproducing the same behaviour, which is worth an assertion because
            // moving the cutoff would silently move this.
            const std::vector<float> later = causticField(140.0f, 1.0f / 12.0f);
            float moved = 0.0f;
            for (std::size_t i = 0; i < count; ++i)
                moved += (later[i] - capped[i]) * (later[i] - capped[i]);

            EXPECT_NEAR(0.5f * moved / spread, 0.511f, 0.03f) << "how much of the pattern is new a twelfth later";
        }

        /// The sun's disc carries exactly its irradiance, however wide the pixel that finds it.
        ///
        /// **The sun is drawn in the sky, not answered by a highlight on each surface that could
        /// reflect it** — so the one thing that must hold is that widening the disc never changes
        /// how much light is in it. A cone twice as wide covers four times the solid angle and has
        /// to be four times dimmer, and that is what makes a rough sea spread the sun without
        /// brightening the sea.
        ///
        /// Two fields of view over the same sun, so the pixel doing the finding is ten times wider
        /// in one than the other. The irradiance is small because nothing here has an exposure stage
        /// and the real thing saturates on sight.
        TEST_F(RtxVisibilityTest, theSunsDiscCarriesItsIrradianceHoweverWideThePixelThatFindsIt)
        {
            constexpr std::uint32_t size = 64;
            constexpr float irradiance = 4.0e-5f;

            struct Disc
            {
                float mPeak;
                float mTotal;
                float mSpreadAngle;
            };

            // Looking straight at a sun 45 degrees up, from clear of the only thing in the scene.
            const auto lookAtTheSun = [&](float fov) {
                Shaders::VisibilityConstants camera = makeCamera(
                    osg::Vec3f(0.0f, -500.0f, 0.0f), osg::Vec3f(0.0f, -501.0f, 1.0f), fov, size, size, 10000.0f);
                camera.mSunDirection = sunAt(osg::DegreesToRadians(45.0f));
                camera.mSunIrradiance = osg::Vec3f(irradiance, irradiance, irradiance);

                const SceneDesc scene = makeWall();
                std::vector<std::uint8_t> pixels;
                countHits(scene, noTextures(), camera, size, pixels);

                // A pixel's solid angle is its side squared at these angles: the frame is two
                // degrees across in one case and twenty in the other, where the cos-cubed the exact
                // form carries is a part in two thousand.
                Disc disc{ .mPeak = 0.0f, .mTotal = 0.0f, .mSpreadAngle = camera.mSpreadAngle };
                for (std::size_t i = 0; i < std::size_t{ size } * size; ++i)
                {
                    const float radiance = decodeSrgb(pixels[i * 4]);
                    disc.mPeak = std::max(disc.mPeak, radiance);
                    disc.mTotal += radiance * camera.mSpreadAngle * camera.mSpreadAngle;
                }
                return disc;
            };

            // The same arithmetic the shader does, from the same two constants: a cap of angular
            // radius `r` subtends `pi * (2 sin(r / 2))^2`, and the disc's radiance is the sun's
            // irradiance spread over it.
            const auto capRadiance = [&](float spreadAngle) {
                const float edge = 2.0f * std::sin(0.5f * (Shaders::SUN_ANGULAR_RADIUS + 0.5f * spreadAngle));
                return irradiance / (0.5f * Shaders::TAU * edge * edge);
            };

            // A byte is worth about 0.006 of a linear value up here, so that is the tolerance.
            const Disc fine = lookAtTheSun(2.0f);
            EXPECT_NEAR(fine.mPeak, capRadiance(fine.mSpreadAngle), 0.007f)
                << "a pixel a third of the sun's width across";

            // **And the light in it is the sun's**, integrated over the frame rather than argued
            // for. Two hundred and fifty-six pixels land inside this disc, so the sum is a real
            // quadrature: it comes out at 4.015e-5 against the 4.0e-5 that was put in.
            EXPECT_NEAR(fine.mTotal, irradiance, 0.03f * irradiance) << "the disc holds what the sun sent";

            // Ten times the field of view, so ten times the pixel and a disc widened by 1.6 — which
            // by the cap's solid angle is 2.28 times dimmer, and measures so. **This is the whole
            // mechanism**: the same flux over a larger cap.
            const Disc coarse = lookAtTheSun(20.0f);
            EXPECT_NEAR(coarse.mPeak, capRadiance(coarse.mSpreadAngle), 0.007f)
                << "a pixel wider than the sun, which averages it rather than sampling it";

            // Its total is not asserted, and the reason is the honest one: this disc covers 5.7
            // pixels and four pixel centres fall inside it, so a sum over pixels is quadrature with
            // four samples and lands 30% low. The peak above is the exact statement.
            EXPECT_LT(coarse.mPeak, fine.mPeak) << "a wider cone is a dimmer sun, never a brighter one";
        }

        /// A rough sea spreads the sun into a road across the water, and a flat one does not.
        ///
        /// **This is what the lost slopes are for.** Water too fine for the ray cone is averaged
        /// into a flat facet, and a facet that lost its slope is a mirror: it reflects the sun as
        /// one hard dot, and as the camera moves that dot jumps between pixels — the field of
        /// crawling white sparks that distant water becomes. Handing the variance of what was
        /// dropped to the sun's disc instead is what turns it into a road.
        ///
        /// Far enough out that the cone cannot resolve the waves, which is where the term decides
        /// the picture. Water and sky only, so every byte in the frame is the sun off the surface:
        /// the refraction ray leaves through the bottom, finds nothing, and comes back black.
        TEST_F(RtxVisibilityTest, aRoughSeaSpreadsTheSunIntoARoadAndAFlatOneShowsOneDot)
        {
            constexpr std::uint32_t size = 64;
            constexpr float irradiance = 1.0e-3f;

            struct Road
            {
                int mPeak;
                std::size_t mLit;
            };

            const auto road = [&](const SeaState& sea) {
                const osg::Vec3f eye(0.0f, 0.0f, 6000.0f);
                const osg::Vec3f at(0.0f, 14000.0f, 0.0f);

                Shaders::VisibilityConstants camera = makeCamera(eye, at, 20.0f, size, size, 1000000.0f);

                // The sun put exactly where the centre pixel's reflection points, which is the view
                // mirrored in the water's plane and then reversed into a direction of travel. Off
                // the top of the frame by twice the camera's own tilt, so the disc in the sky and
                // the disc in the water are never in the same picture.
                osg::Vec3f view = at - eye;
                view.normalize();
                camera.mSunDirection = osg::Vec3f(-view.x(), -view.y(), view.z());
                camera.mSunIrradiance = osg::Vec3f(irradiance, irradiance, irradiance);
                camera.mWaterLevel = 0.0f;

                const SceneDesc scene = makeOpenWater(20000.0f);
                std::vector<std::uint8_t> pixels;
                countHits(scene, noTextures(), camera, size, pixels, sea);

                Road found{ .mPeak = 0, .mLit = 0 };
                for (std::size_t i = 0; i < std::size_t{ size } * size; ++i)
                {
                    found.mPeak = std::max(found.mPeak, int{ pixels[i * 4] });
                    found.mLit += pixels[i * 4] > 0 ? 1 : 0;
                }
                return found;
            };

            // A flat sea is a mirror and shows the sun once: four pixels of 4,096, at 202 of 255.
            const Road still = road(SeaState{ .mSignificantHeight = 0.0f });
            EXPECT_LT(still.mLit, 20u) << "measured 4 pixels: a mirror shows one dot";
            EXPECT_GT(still.mPeak, 150) << "measured 202: and shows it at full strength";

            // The same sun over a sea with a state in it reaches 1,914 pixels — near half the frame
            // — at a peak of 10. **Nearly five hundred times the area at a twentieth the strength**,
            // which is a road rather than a spark.
            const Road running = road(SeaState{});
            EXPECT_GT(running.mLit, 1000u) << "measured 1914 pixels: the sun spread across the water";
            EXPECT_LT(running.mPeak, 40) << "measured 10: and no pixel of it near the mirror's";
        }

        /// Water too fine for the cone to resolve widens the cone that refracts through it.
        ///
        /// **What the cone could not resolve is not gone, it is rough** — and that roughness has to
        /// reach the texture filter, not only the specular lobe. A seabed seen through a mile of
        /// ruffled water is blurred by the slopes that were averaged away, and read at its sharpest
        /// mip instead it comes back as crawling detail no filter downstream can take out.
        ///
        /// **The sea here is built so that every wave is past the cone's reach.** The spectrum runs
        /// from 32 units up to its peak, so with a peak of 64 a footprint of 66 resolves none of it:
        /// every slope is averaged out and the surface is geometrically flat — the normal is exactly
        /// up, the refraction goes straight down, and nothing wobbles — while the whole spectrum's
        /// variance is still carried as roughness. That leaves the cone's width as the only thing
        /// that differs from a still sea, and a ladder of mips on the bed to read it off.
        TEST_F(RtxVisibilityTest, waterTooFineToResolveWidensTheConeItRefractsThrough)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;
            constexpr float height = 12000.0f;
            constexpr float depth = 1500.0f;

            Device& device = *mHarness->mDevice;
            CommandPool pool(device);
            const TextureArray textures = makeMipLadder(device, pool);

            // A fifth of a degree off the vertical, which is the least `makeCamera` will take.
            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -50.0f, height), osg::Vec3f(0.0f, 0.0f, -depth), 20.0f, size, size, 100000.0f);
            camera.mWaterLevel = 0.0f;

            // Water over a bed that glows its own ladder. Emissive rather than lit, so the byte is
            // the texture and nothing else: an unlit albedo would need a light, and a light would
            // put its own falloff between the mip and the measurement.
            const auto level = [&](const SeaState& sea) {
                SceneDesc scene = makeOpenWater(4000.0f);

                const Index bed = scene.addMesh(makeSheet(500.0f, -depth), {}, sQuadUv, sQuadIndices);
                const Index ladder = scene.addMaterial(
                    Material{ .mEmissive = scene.addTexture(VFS::Path::NormalizedView("ladder.dds")) });
                scene.addInstance(
                    MeshInstance{ .mTransform = osg::Matrixf::identity(), .mMesh = bed, .mMaterial = ladder });

                std::vector<std::uint8_t> pixels;
                countHits(scene, textures, camera, size, pixels, sea);

                // Back out everything between the texture and the byte: the emissive scale, the
                // water the glow crossed on its way up, and the two per cent the surface reflected.
                const float carried = Shaders::EMISSIVE_INTENSITY * std::exp(-Shaders::WATER_EXTINCTION[1] * depth)
                    * (1.0f - Shaders::WATER_F0);
                return ladderLevel(decodeSrgb(pixels[centre + 1]) / carried);
            };

            // How wide the refraction's cone is where it lands, in world units: the pixel's own
            // footprint where it met the water, plus what it gained over the leg down. **Twice the
            // lobe**, because the lobe is an rms angle from the axis and a cone spread is a width —
            // and a quarter of it to begin with, because refraction bends by `1 - 1 / n` of what
            // reflection does, so what is seen *through* a rough surface is blurred that much less.
            const auto coneAtBed = [&](float lobe) {
                const float bent = lobe * (1.0f - 1.0f / Shaders::WATER_IOR);
                return camera.mSpreadAngle * height + (camera.mSpreadAngle + 2.0f * bent) * depth;
            };

            const SeaState fine{ .mSignificantHeight = 3.0f, .mPeakWavelength = 64.0f };
            const float still = level(SeaState{ .mSignificantHeight = 0.0f });
            const float ruffled = level(fine);

            // The ladder has seven levels and the readout is only meaningful off both ends of it.
            EXPECT_GT(still, 1.0f) << "measured 2.26, clear of the sharpest mip";
            EXPECT_LT(ruffled, 5.0f) << "measured 3.71, clear of the coarsest";

            // Mip level is the log of the cone's width, so the difference is the log of the ratio —
            // and the base term, which is the triangle's texels against its world area, cancels.
            // Predicted 1.474 from the wave table the shader was handed; measured 1.449, which is
            // three quarters of a byte on a ladder 30 bytes to the level.
            //
            // **The factor of two is what this pins.** Adding the lobe once instead of twice puts
            // the prediction at 0.918, sixteen bytes from what the frame actually reads.
            EXPECT_NEAR(ruffled - still, std::log2(coneAtBed(lobeOf(fine)) / coneAtBed(0.0f)), 0.08f)
                << "the cone widened by twice the rms slope the sea could not show";
        }

        /// Fog takes what Beer-Lambert says over the path and gives its own colour back in place.
        ///
        /// **A horizontal ray is what makes the march exact.** The layer's density varies only with
        /// height, so along a ray that holds its own the medium is uniform — and then the march
        /// telescopes: the per-step transmittances multiply to `exp(-sigma * span)` whatever the
        /// step distribution is, and the scattered terms sum to `colour * (1 - T)`. So this asserts
        /// the analytic answer rather than the march's approximation of it.
        TEST_F(RtxVisibilityTest, fogTakesWhatBeerLambertSaysOverThePathAndGivesItsOwnColourBack)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;
            constexpr float distance = 2000.0f;
            constexpr float extinction = 3.5e-4f;

            const auto look = [&](float thickness) {
                Shaders::VisibilityConstants camera = makeCamera(
                    osg::Vec3f(0.0f, -distance, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                litThroughFog(camera, thickness);

                const SceneDesc scene = makeWall();
                std::vector<std::uint8_t> pixels;
                countHits(scene, noTextures(), camera, size, pixels);
                return std::array<int, 3>{ pixels[centre], pixels[centre + 1], pixels[centre + 2] };
            };

            // The wall is untextured, so its albedo is 0.5 and the cell's ambient is all that is on
            // it: 0.5 * 0.6 = 0.3. Over that sits `exp(-3.5e-4 * 2000)` = 0.4966 of transmittance,
            // with the rest of the path's worth of the fog's own colour in front of it.
            const float transmittance = std::exp(-extinction * distance);
            const std::array<int, 3> fogged = look(extinction);
            constexpr float wall = 0.5f * sFoggySky;
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto expected
                    = static_cast<int>(encodeSrgb(wall * transmittance + sHaze[channel] * (1.0f - transmittance)));

                EXPECT_NEAR(fogged[channel], expected, 1) << "channel " << channel;
            }

            // The three channels also have to come apart the way the fog's colour does, or a grey
            // haze would pass the arithmetic above by accident.
            EXPECT_LT(fogged[0], fogged[1]) << "the fog's own colour, showing through";
            EXPECT_LT(fogged[1], fogged[2]);

            // And no fog is *exactly* no fog. A lit surface with air over it is a differently lit
            // one, which is why every test here that measures radiance leaves this at zero.
            const std::array<int, 3> clear = look(0.0f);
            for (const int level : clear)
                EXPECT_EQ(level, int{ encodeSrgb(wall) }) << "with no fog in the cell";
        }

        /// The fog layer sits on the water, thins with height above it, and stops at the surface.
        ///
        /// Fog gathers over water and drains off high ground, so the level a cell records is where
        /// its layer sits — and *at* it rather than in it, because a point under the surface already
        /// has the water's own absorption over it. Fog there would be a second medium laid on the
        /// first, putting grey between the eye and the seabed twice over.
        TEST_F(RtxVisibilityTest, theFogLayerSitsOnTheWaterThinsAboveItAndStopsAtIt)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;
            constexpr float distance = 2000.0f;
            constexpr float extinction = 3.5e-4f;

            // The ray runs level at z = 0, so how much fog it crosses is decided by where the layer
            // is put under it and by nothing else.
            const auto look = [&](float level, float thickness) {
                Shaders::VisibilityConstants camera = makeCamera(
                    osg::Vec3f(0.0f, -distance, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                litThroughFog(camera, thickness, level);

                const SceneDesc scene = makeWall();
                std::vector<std::uint8_t> pixels;
                countHits(scene, noTextures(), camera, size, pixels);
                return std::array<int, 3>{ pixels[centre], pixels[centre + 1], pixels[centre + 2] };
            };

            // A dry cell is handed minus infinity, and falls back to sea level — which is where a
            // water level of zero puts the layer anyway, so the two have to agree exactly.
            const std::array<int, 3> dry = look(-std::numeric_limits<float>::infinity(), extinction);
            const std::array<int, 3> atSeaLevel = look(0.0f, extinction);
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_EQ(dry[channel], atSeaLevel[channel]) << "channel " << channel << ", the dry-cell fallback";

            // Three thousand units under the ray, so it runs `exp(-3000 / 2600)` = 0.3154 of the way
            // up the layer's own falloff and crosses less than a third of the fog.
            constexpr float below = 3000.0f;
            const float thinned = extinction * std::exp(-below / Shaders::FOG_HEIGHT);
            const float transmittance = std::exp(-thinned * distance);

            const std::array<int, 3> high = look(-below, extinction);
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto expected = static_cast<int>(
                    encodeSrgb(0.5f * sFoggySky * transmittance + sHaze[channel] * (1.0f - transmittance)));

                EXPECT_NEAR(high[channel], expected, 1) << "channel " << channel << ", high over the layer";
            }

            // And with the surface over the ray instead there is no air to fog at all: the frame is
            // whatever the water did to it and nothing else, whatever the cell's fog says.
            const std::array<int, 3> submerged = look(100.0f, extinction);
            const std::array<int, 3> withoutFog = look(100.0f, 0.0f);
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_EQ(submerged[channel], withoutFog[channel]) << "channel " << channel << ", under the surface";
        }

        /// A lamp lights the air it stands in, and by the isotropic share of what it delivers.
        ///
        /// **`INV_FOUR_PI` is what this is really about.** A lamp reaches a point in the fog as
        /// irradiance, exactly as it reaches a surface, and what comes back toward the eye is that
        /// irradiance spread over the whole sphere. Dropping the factor is not subtle — it lights
        /// the air 4pi times over, and the centre pixel here goes from 121 of 255 to 211.
        ///
        /// **The lamp is put far enough away that its falloff is flat along the ray**, which is what
        /// makes the march's answer analytic: with the light it delivers constant, the scattered
        /// terms telescope to `E / 4pi * (1 - T)` exactly as a constant fog colour does. Twenty
        /// thousand units off a two-hundred-unit ray varies the irradiance by 0.02%.
        TEST_F(RtxVisibilityTest, aLampLightsTheAirItStandsInByTheIsotropicShareOfWhatItDelivers)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;
            constexpr float distance = 200.0f;
            constexpr float reach = 30000.0f;

            // Half the ray's worth of fog, so the wall and the air contribute comparably.
            const float extinction = std::log(2.0f) / distance;

            // Behind the wall in y, so its cosine there is negative and the lamp lights the air
            // without also lighting what the air is in front of.
            const osg::Vec3f lamp(0.0f, 100.0f, 20000.0f);

            // What one unit of intensity delivers at the middle of the ray, from the same windowed
            // inverse square the shader uses: an inverse square that reaches exactly zero at the
            // light's reach, because Morrowind's is a hard cutoff and clipping one leaves a ring.
            const osg::Vec3f middle(0.0f, -0.5f * distance, 0.0f);
            const float span = (lamp - middle).length();
            const float ratio = span / reach;
            const float window = 1.0f - ratio * ratio * ratio * ratio;
            const float delivered = window * window / (span * span + 1.0f);

            const auto look = [&](bool lit) {
                SceneDesc scene = makeWall();
                if (lit)
                    scene.addLight(Light{
                        .mPosition = lamp,
                        // Scaled so the lamp delivers exactly one unit of irradiance to the ray,
                        // which is what lets the expectation below be written without it.
                        .mIntensity = osg::Vec3f(1.0f, 1.0f, 1.0f) / delivered,
                        .mReach = reach,
                    });

                Shaders::VisibilityConstants camera = makeCamera(
                    osg::Vec3f(0.0f, -distance, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
                litThroughFog(camera, extinction);

                // Black air, so the lamp is the only thing in the frame the fog scatters and the
                // expectation below needs no term for the haze `litThroughFog` would otherwise put
                // in it.
                camera.mFogColour = osg::Vec3f();

                std::vector<std::uint8_t> pixels;
                countHits(scene, noTextures(), camera, size, pixels);
                return int{ pixels[centre] };
            };

            // With no lamp the air scatters nothing, because the fog's own colour is black here: the
            // wall is all there is, at half of it.
            constexpr float wall = 0.5f * sFoggySky;
            EXPECT_EQ(look(false), int{ encodeSrgb(0.5f * wall) }) << "black air over a lit wall";

            // And with it, one unit of irradiance through an isotropic sphere, over the half of the
            // ray's light the fog took: 0.15 + 1 / (4 pi) * 0.5 = 0.18979, which encodes to 121.
            const float scattered = 0.25f * Shaders::INV_PI * 0.5f;
            EXPECT_NEAR(look(true), int{ encodeSrgb(0.5f * wall + scattered) }, 1) << "the lamp in the air";
        }

        /// The banked field holds as much air as an even one, which is what `FOG_COVERAGE` is for.
        ///
        /// **The noise redistributes the fog, it does not remove it.** The extinction the host
        /// derived is Morrowind's own view distance turned into a coefficient, and a band that
        /// leaves 40% of the volume clear would silently make the world twice as clear as the game
        /// says. Dividing the coverage by its own mean is what holds the average where it was — and
        /// this is what makes that constant a measurement rather than a note, since moving the band
        /// without re-measuring moves this ratio by the constant's own error.
        ///
        /// **It settles just under one, and that is Jensen's inequality rather than a mistake.** A
        /// banked field's optical depth varies far more than an even one's, and `exp` is convex, so
        /// more light survives the same *average* density. Measured at 0.971 against a thickness of
        /// 0.09 — and the deficit scales with it, so a thinner fog would sit closer to one.
        ///
        /// Nine viewpoints, because one is not a sample: the steps bunch near the camera, so a
        /// single frame weighs one small volume of the field heavily and lands anywhere within six
        /// per cent. Nine brings that to about two.
        TEST_F(RtxVisibilityTest, theBankedFieldHoldsAsMuchAirAsAnEvenOne)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::size_t count = std::size_t{ size } * size;

            const auto air = [&](float uniform, float where) {
                Shaders::VisibilityConstants camera = makeCamera(osg::Vec3f(where, -50000.0f, 0.0f),
                    osg::Vec3f(where, -60000.0f, 0.0f), 90.0f, size, size, 100000.0f);
                camera.mFogColour = osg::Vec3f(1.0f, 1.0f, 1.0f);
                camera.mFogExtinction = 3.0e-6f;
                camera.mFogUniform = uniform;

                // A wall behind the camera, because a scene has to hold something. Every ray runs
                // to `FOG_REACH` and comes back with air and the sky, which is what makes the
                // frame's mean a measurement of the air alone.
                const SceneDesc scene = makeWall();
                std::vector<std::uint8_t> pixels;
                countHits(scene, noTextures(), camera, size, pixels);

                double total = 0.0;
                for (std::size_t i = 0; i < count; ++i)
                    total += double{ decodeSrgb(pixels[i * 4]) };

                return total / static_cast<double>(count);
            };

            double ratio = 0.0;
            constexpr std::array places{ 0.0f, 12345.0f, -31000.0f, 77000.0f, 250000.0f, -140000.0f, 33000.0f,
                -420000.0f, 610000.0f };
            for (const float where : places)
                ratio += air(0.0f, where) / air(1.0f, where);

            ratio /= static_cast<double>(places.size());

            EXPECT_NEAR(ratio, 0.971, 0.05) << "banked air against even air, over nine viewpoints";
        }

        /// The fog scatters the sun forward far harder than back, which is what a Mie phase is for.
        ///
        /// **A single Henyey-Greenstein lobe cannot do this shape.** Real droplets throw a peak
        /// within a degree of the light that is orders of magnitude above anything one `g` reaches,
        /// and still send a sixth of isotropic *backwards* — the blaze around a low sun, and fog not
        /// going black when you turn away from it. Before this the fog was lit by the sky and the
        /// lamps and not at all by the sun, so facing it rendered identically to facing away.
        ///
        /// **A ratio, because it is the only thing the fixture computes exactly.** Two frames differ
        /// in nothing but which side of the camera the sun is on, at the same elevation — so the
        /// column of fog it crosses, the extinction, the transmittance and the march all cancel, and
        /// what is left is `p(26.6 degrees) / p(153.4)`. An isotropic fog would give exactly one.
        TEST_F(RtxVisibilityTest, theFogScattersTheSunForwardFarHarderThanBack)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;

            // Bright enough to read against eight bits after the fog's own column has taken 83% of
            // it, and the forward case still has to stay inside one.
            constexpr float irradiance = 12.7f;

            // Level, so the centre ray holds one height and the medium along it is uniform — which
            // is what lets the two frames cancel to the phase function alone.
            const auto lookPast = [&](float towardsY) {
                Shaders::VisibilityConstants camera = makeCamera(
                    osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1000.0f, 0.0f), 60.0f, size, size, 100000.0f);

                // The sun a quarter of the way up, ahead of the camera or behind it. Both carry the
                // same climb, so `fogSunDepth` is the same for each and cancels.
                osg::Vec3f towards(0.0f, towardsY, 0.5f);
                towards.normalize();
                camera.mSunDirection = -towards;
                camera.mSunIrradiance = osg::Vec3f(irradiance, irradiance, irradiance);
                camera.mFogExtinction = 3.0e-4f;
                camera.mFogUniform = 1.0f;

                // Nothing to hit and nothing else to scatter: every ray runs out to `FOG_REACH`
                // through even air whose own colour is black, so the pixel is the sun and no more.
                //
                // **Out of reach of the shadow rays and not merely out of the frame.** The march
                // sends one at the sun from every stretch of the ray, and a wall standing at y = 0
                // is squarely in the path of the ones the *backward* case sends — so it shadowed the
                // near steps, at a march offset that decides which, and the ratio below moved by a
                // sixth with the dither. A sheet below the world is past `mFar` in every direction
                // any ray here travels.
                SceneDesc scene;
                scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::identity(),
                    .mMesh = scene.addMesh(makeSheet(4000.0f, -200000.0f), {}, {}, sQuadIndices) });

                std::vector<std::uint8_t> pixels;
                countHits(scene, noTextures(), camera, size, pixels);

                return decodeSrgb(pixels[centre]);
            };

            const float ahead = lookPast(1.0f);
            const float behind = lookPast(-1.0f);

            // Hand-computed from Jendersie and d'Eon's fit at a droplet diameter of eight microns:
            // the blend is 47.4% Draine over a Henyey-Greenstein peak of g = 0.98447, and at
            // `cos = +-0.8944` that comes to 0.225158 forward against 0.011708 back.
            constexpr float forward = 0.225158f;
            constexpr float backward = 0.011708f;

            EXPECT_NEAR(ahead / behind, forward / backward, 1.0f) << "forward against backward scattering";

            // **And the absolute value, because a ratio cannot see a factor of `4 pi`.** That is the
            // mistake this shape of function invites: normalise the phase so isotropic reads one —
            // the convention a lamp is written in — and both frames grow together while their ratio
            // stays put. What the eye gets is the sun's irradiance times the phase *per steradian*,
            // less the fog's own column on the way down, over a ray that runs to `FOG_REACH`:
            //
            //   12.7 * 0.225158 * exp(-2600 * 3e-4 / 0.44721) * (1 - exp(-3e-4 * 30000)) = 0.4998
            //
            // which the sRGB curve puts at 188 of 255. Four pi times that is white.
            const float climb = 0.5f / std::sqrt(1.25f);
            const float column = std::exp(-Shaders::FOG_HEIGHT * 3.0e-4f / climb);
            const float crossed = 1.0f - std::exp(-3.0e-4f * Shaders::FOG_REACH);

            EXPECT_NEAR(ahead, irradiance * forward * column * crossed, 0.006f)
                << "the sun's own irradiance through the phase function, per steradian";

            // And the backward half is not nothing, which is the other half of why a single lobe
            // will not do: fog behind you still glows.
            EXPECT_GT(behind, 0.01f) << "a sixth of isotropic still comes back";
        }

        /// A lid over the march takes the sun out of the air beneath it, and takes all of it.
        ///
        /// **Exactly what sunless air scatters, not merely less than open air.** A shaft that leaked
        /// a tenth of the sun would still be darker than the open sky beside it, and an assertion
        /// that only asked for darker would pass while it leaked. The lid spans the whole march and
        /// the camera's own ray never reaches it — it runs level while the lid is five hundred units
        /// overhead — so what changes between the two frames is the shadow ray and nothing else.
        TEST_F(RtxVisibilityTest, aLidOverTheMarchTakesTheSunOutOfTheAirBeneathIt)
        {
            constexpr std::uint32_t size = 33;
            constexpr std::size_t centre = (std::size_t{ size / 2 } * size + size / 2) * 4;
            constexpr float irradiance = 4.0f;

            const auto look = [&](bool lidded, bool lit) {
                // The same sheet either way, over the march or under it, so the two frames differ
                // in what the shadow ray finds and in nothing else — not in what is in the scene,
                // nor in how large it is.
                SceneDesc scene;
                scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::identity(),
                    .mMesh = scene.addMesh(makeSheet(40000.0f, lidded ? 500.0f : -500.0f), {}, {}, sQuadIndices) });

                Shaders::VisibilityConstants camera = makeCamera(
                    osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1000.0f, 0.0f), 60.0f, size, size, 100000.0f);

                // Ahead of the camera and well up, so the phase function has something to scatter
                // forward and every shadow ray still climbs into the lid.
                osg::Vec3f travelling(0.0f, -0.6f, -0.8f);
                travelling.normalize();
                camera.mSunDirection = travelling;
                camera.mSunIrradiance = lit ? osg::Vec3f(irradiance, irradiance, irradiance) : osg::Vec3f();

                // Even air with a colour of its own, so the frame is never empty and the two sunless
                // cases have something to agree about.
                camera.mFogColour = osg::Vec3f(0.02f, 0.02f, 0.02f);
                camera.mFogExtinction = 2.0e-4f;
                camera.mFogUniform = 1.0f;

                std::vector<std::uint8_t> pixels;
                countHits(scene, noTextures(), camera, size, pixels);
                return int{ pixels[centre] };
            };

            const int open = look(false, true);
            const int shaded = look(true, true);
            const int sunless = look(true, false);

            EXPECT_EQ(shaded, sunless) << "the lid takes all of the sun, not most of it";
            EXPECT_GT(open, shaded + 20) << "and there was a sun to take";
        }

        /// Its own device, because the validation layers allocate and this test counts allocations.
        class RtxFrameCostTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                std::string reason;
                mHarness = Testing::getUnvalidatedHarness(reason);
                if (mHarness == nullptr)
                    GTEST_SKIP() << reason;
            }

            Testing::Harness* mHarness = nullptr;
        };

        /// A frame that changes nothing must not go to the heap.
        ///
        /// The concern is jitter rather than throughput: at sixty frames a second a single
        /// allocator stall is a dropped frame, and an average hides it. What this forbids on the
        /// frame path is a `std::string` built, an unreserved vector grown, a `std::function`
        /// captured, a `make_unique` reached for, or logging that did not compile out.
        ///
        /// Warmed up first, because the first of anything legitimately allocates: descriptor pools
        /// grow, the driver caches its first call, and a command buffer finds its size.
        TEST_F(RtxFrameCostTest, aSteadyFrameDoesNotTouchTheHeap)
        {
            constexpr std::uint32_t size = 64;
            constexpr int warmUpFrames = 8;
            constexpr int measuredFrames = 32;

            // What a frame is allowed, and why. Zero is the claim; a constant rather than a literal
            // zero is so that a path which must allocate can be admitted deliberately, with the
            // number and the reason written down beside it.
            constexpr std::size_t budgetPerFrame = 0;

            const SceneDesc scene = makeWall();

            Device& device = *mHarness->mDevice;
            CommandPool pool(device);
            const SceneAcceleration acceleration(device, pool, scene);
            const SceneBuffers buffers(device, pool, scene, acceleration.getIndices());

            const TextureArray textures(device, std::vector<Texture>{});
            const VisibilityPass pass(device, Testing::getShaderDirectory(), textures.getLayout());
            const VisibilityInputs inputs{
                .mScene = acceleration.getTopLevel(),
                .mBuffers = &buffers,
                .mTextures = textures.getSet(),
            };

            Image target(device, size, size, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            Image history(device, size, size, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT);
            const Buffer hits(device, sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            pool.submitAndWait([&](VkCommandBuffer commands) {
                target.transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            });

            // A command buffer reused against a fence, which is what a frame is and what the window
            // loop does. `submitAndWait` would be the setup shape — a fresh buffer from the pool and
            // a wait on the whole queue — and it is measured here as allocating nothing either, so
            // this is about what is being pinned rather than about what it costs today.
            const std::vector<VkCommandBuffer> recorded = pool.allocate(1);
            const VkCommandBuffer commands = recorded.front();

            const VkFenceCreateInfo describeFence{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            VkFence finished = VK_NULL_HANDLE;
            ASSERT_EQ(vkCreateFence(device.getHandle(), &describeFence, nullptr, &finished), VK_SUCCESS);

            // Everything a still frame does: the camera worked out, the pass recorded, the work
            // submitted and waited on. The camera is the same every time, which is what "steady"
            // means — a moving one would still allocate nothing, but then nothing would be pinned.
            const auto frame = [&] {
                const Shaders::VisibilityConstants camera = makeCamera(
                    osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);

                vkResetCommandBuffer(commands, 0);
                const VkCommandBufferBeginInfo begin{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                };
                vkBeginCommandBuffer(commands, &begin);
                pass.record(commands, inputs, target, history, hits, camera);
                vkEndCommandBuffer(commands);

                const VkCommandBufferSubmitInfo buffer{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                    .commandBuffer = commands,
                };
                const VkSubmitInfo2 submit{
                    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                    .commandBufferInfoCount = 1,
                    .pCommandBufferInfos = &buffer,
                };
                vkQueueSubmit2(device.getQueue(), 1, &submit, finished);
                vkWaitForFences(device.getHandle(), 1, &finished, VK_TRUE, ~std::uint64_t{ 0 });
                vkResetFences(device.getHandle(), 1, &finished);
            };

            for (int i = 0; i < warmUpFrames; ++i)
                frame();

            const std::size_t before = Testing::getAllocationCount();
            for (int i = 0; i < measuredFrames; ++i)
                frame();
            const std::size_t after = Testing::getAllocationCount();

            vkDestroyFence(device.getHandle(), finished, nullptr);

            EXPECT_LE(after - before, budgetPerFrame * measuredFrames)
                << (after - before) << " allocations across " << measuredFrames << " frames";
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

            SceneDesc placedByInstance;
            placedByInstance.addInstance(MeshInstance{
                .mTransform = transform, .mMesh = placedByInstance.addMesh(local, {}, {}, sQuadIndices) });

            std::array<osg::Vec3f, 4> moved{};
            for (std::size_t i = 0; i < local.size(); ++i)
                moved[i] = local[i] * transform;

            SceneDesc placedByVertex;
            placedByVertex.addInstance(MeshInstance{
                .mTransform = osg::Matrixf::identity(), .mMesh = placedByVertex.addMesh(moved, {}, {}, sQuadIndices) });

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

            SceneDesc scene;
            const Index mesh = scene.addMesh(positions, {}, {}, sQuadIndices);
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
