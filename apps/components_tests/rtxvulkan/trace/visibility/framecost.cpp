#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Matrixf>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/rendererprobe.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sprite.hpp>
#include <components/rtx/scene/surface.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/texture/texture.hpp>
#include <components/rtxvulkan/texture/texturearrival.hpp>
#include <components/rtxvulkan/texture/texturepasses.hpp>
#include <components/vfs/pathutil.hpp>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        /// Nothing here loads the validation layers, because they allocate and this is what counts allocations.
        /// The frame is measured through `getUnvalidatedRenderer`, which the fixture does not supply; the
        /// fixture supplies the raw device the texture array below is built on. Both are unvalidated for the
        /// reason `getUnvalidatedHarness` states with the number.
        struct RtxFrameCostTest : DeviceTest
        {
            RtxFrameCostTest()
                : DeviceTest(false)
            {
            }
        };

        /// A warm renderer draws a still frame without going to the heap once.
        ///
        /// The concern is jitter and not throughput: at sixty frames a second one allocator stall is a dropped
        /// frame and an average hides it. Forbidden on the frame path: a `std::string` built, an unreserved
        /// vector grown, a `std::function` captured, a `make_unique`, or logging that did not compile out.
        ///
        /// Through `Rtx::Renderer` and not the passes it owns: a hand-assembled frame is a second copy of the
        /// frame path that could keep passing while the real one grew an allocating pass, and would leave out
        /// the fog volume, accumulator, wavelet, bloom, tone curve, exposure, the ring and `finishFrame`.
        ///
        /// Five shapes, because they light different code: a plain frame; a filtered one (the wavelet's levels
        /// and history); an accumulating one (the sum image); a body walking, the one thing the frame path
        /// computes rather than copies (a pose, a dispatch, a refit, a rebuild of what moved); and a storm
        /// thickening and thinning, every table a frame writes changing length below its warm-up high-water
        /// mark. The camera stands still through all five, which is what pins them. Warmed first, because the
        /// first of anything legitimately allocates (descriptor pools, driver caches, command buffer size).
        TEST_F(RtxFrameCostTest, aWarmRendererDrawsAStillFrameWithoutTheHeap)
        {
            constexpr std::uint32_t size = 64;
            constexpr int warmUpFrames = 16;
            constexpr int measuredFrames = 32;

            // What a frame is allowed, and why. Zero is the claim; a constant rather than a literal
            // zero is so that a path which must allocate can be admitted deliberately, with the
            // number and the reason written down beside it.
            constexpr std::size_t budgetPerFrame = 0;

            VulkanRenderer& renderer = getUnvalidatedRenderer();

            // A wall to trace, and a second one behind it skinned to one bone so that there is a body
            // to pose.
            SceneDesc scene = makeWall();
            const Index body
                = addOneBoneBody(scene, MeshArrays{ .mPositions = sWallQuad, .mIndices = sQuadIndices }).mMesh;
            scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::translate(0.0f, 100.0f, 0.0f), .mMesh = body });
            poseByOneBone(scene, body, osg::Matrixf::identity());
            const Index puff = scene.textures().add(VFS::Path::NormalizedView("puff.dds"));

            renderer.resize(size, size);
            renderer.setScene(Rtx::SceneSlot::world(), scene, {});

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -100.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 10000.0f);
            camera.mSun.mIrradiance = osg::Vec3f(2.0f, 2.0f, 2.0f);

            // The sampler's own frame, which advances across every leg: two legs handed the same one
            // would be two legs drawing one picture, and a renderer that noticed could skip work this
            // is measuring.
            std::uint32_t index = 0;
            float walked = 0.0f;

            // What the game's walk does to a body every frame: a new pose, placed.
            const auto walk = [&] {
                walked += 1.0f;
                scene.clearPlacement();
                poseByOneBone(scene, body, osg::Matrixf::translate(0.0f, walked, 0.0f));
                renderer.placeScene(Rtx::SceneSlot::world(), scene);
            };

            // Sixty-four sprites on the first frame and fewer on every one after, a different count
            // each: the warm-up sets the mark and the measured frames stay under it.
            constexpr std::uint32_t mostSprites = 64;
            std::uint32_t storm = 0;
            std::vector<Sprite> sprites;
            sprites.reserve(2 * mostSprites);
            const auto rain = [&](std::uint32_t count) {
                sprites.clear();
                for (std::uint32_t at = 0; at < count; ++at)
                    sprites.push_back(Sprite{ .mPosition = osg::Vec3f(static_cast<float>(at) - 32.0f, 50.0f, 0.0f),
                        .mRadius = 2.0f,
                        .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                        .mAlpha = 0.5f });

                scene.clearPlacement();
                scene.addEmitter(sprites, puff, BlendKind::Over);
                renderer.placeScene(Rtx::SceneSlot::world(), scene);
            };
            const auto thicken = [&] { rain(mostSprites - storm++ % mostSprites); };

            const auto still = [] {};

            const auto measure = [&](const char* what, const FrameOptions& options, const auto& change) {
                const auto frame = [&] {
                    change();

                    camera.mFrame = index++;
                    RendererProbe(renderer).traceFrame(camera, options);
                    renderer.finishFrame();
                };

                for (int i = 0; i < warmUpFrames; ++i)
                    frame();

                const std::size_t before = Testing::getAllocationCount();
                for (int i = 0; i < measuredFrames; ++i)
                    frame();
                const std::size_t after = Testing::getAllocationCount();

                EXPECT_LE(after - before, budgetPerFrame * measuredFrames)
                    << what << ": " << (after - before) << " allocations across " << measuredFrames << " frames";
            };

            measure(
                "a plain frame", FrameOptions{ .mReconstruction = ReconstructionRequest{ .mDenoise = false } }, still);
            measure("a filtered frame", FrameOptions{ .mReconstruction = ReconstructionRequest{ .mDenoise = true } },
                still);
            measure("an accumulating frame",
                FrameOptions{ .mAccumulate = 1, .mReconstruction = ReconstructionRequest{ .mDenoise = true } }, still);
            measure(
                "a body walking", FrameOptions{ .mReconstruction = ReconstructionRequest{ .mDenoise = true } }, walk);
            measure("a storm thickening and thinning",
                FrameOptions{ .mReconstruction = ReconstructionRequest{ .mDenoise = true } }, thicken);

            // **And one that keeps thickening, a new high every frame.** Past sixty-four every table
            // a frame writes doubles its room at the first new high, so the thirty-two highs from
            // ninety-seven to a hundred and twenty-eight are all inside it. A table grown to each
            // frame's exact size made itself again on every one of them.
            std::uint32_t rising = mostSprites;
            const auto rise = [&] {
                rain(++rising);
                camera.mFrame = index++;
                RendererProbe(renderer).traceFrame(camera, FrameOptions{});
                renderer.finishFrame();
            };
            for (int i = 0; i < 32; ++i)
                rise();
            const std::size_t risen = Testing::getAllocationCount();
            for (int i = 0; i < 32; ++i)
                rise();
            EXPECT_EQ(rising, 2 * mostSprites);
            EXPECT_EQ(Testing::getAllocationCount() - risen, 0u) << "a new high inside the room a doubling made";

            // **And the read back, which the harness does every frame and the window does never.**
            // Warmed by one call, because the first sizes the caller's vector.
            std::vector<std::uint8_t> pixels;
            renderer.readPixels(pixels);

            const std::size_t before = Testing::getAllocationCount();
            renderer.readPixels(pixels);
            EXPECT_EQ(Testing::getAllocationCount() - before, 0u) << "a second read back into the same vector";
        }

        /// A texture arriving costs the device objects it is and nothing besides. The arrival frame has the
        /// least room: a cell crossing writes a couple of hundred textures into the array on one frame, and a
        /// vector the write grows would be growth on top of the upload. What is left is what a texture is: two
        /// images and the staging buffer their bytes travel in, each a device allocation the host holds a handle
        /// for. Warmed with one texture first, so the array's scratch and descriptor pool have reached size.
        TEST_F(RtxFrameCostTest, aTextureArrivingCostsWhatATextureIs)
        {
            constexpr std::uint32_t extent = 4;
            constexpr std::uint32_t slots = 64;
            constexpr std::uint32_t measured = 32;

            // What one texture is allowed: two images (each a `unique_ptr` for a device object), the staging
            // buffer, and the names those carry, which a build that names objects makes strings for
            // (`Rtx::sDebugNames`), measured at five apiece; the sixth is the batch's and the graveyard's lists.
            // Nothing may grow per texture, and a seventh is a buffer somebody made per arrival.
            constexpr std::size_t budgetPerTexture = 6;

            Device& device = getDevice();
            CommandPool& pool = getPool();

            // Flat and uncompressed, so the description is exact arithmetic rather than a file.
            const std::vector<std::uint8_t> texels(std::size_t{ extent } * extent * 4, 0xFF);
            const std::array<MipLevel, 1> levels{ MipLevel{ 0, extent, extent } };

            const auto describe = [&](std::uint32_t slot) {
                return TextureData{
                    .mSlot = slot,
                    .mFormat = TextureFormat::Rgba8Unorm,
                    .mWidth = extent,
                    .mHeight = extent,
                    .mBytes = std::as_bytes(std::span(texels)),
                    .mLevels = levels,
                    .mName = "arrival",
                };
            };

            const SetLayout layout = TextureArray::describeLayout(device);
            const TexturePasses passes(device);
            TextureArrival rooms(device);
            Batch setup(pool);
            TextureArray array(device, setup, layout, passes, rooms, slots);
            setup.flush();

            // Held across the arrivals as a scene holds its own, and never written to: a device
            // with room refuses nothing.
            std::vector<Refusal> refused;
            const auto arrive = [&](Batch& batch, std::uint32_t slot) {
                const TextureData described = describe(slot);
                array.write(batch, passes, rooms, std::span(&described, 1), refused, nullptr);
            };

            {
                Batch warm(pool);
                arrive(warm, 0);
                warm.flush();
            }

            // **One batch for all of them, which is how a cell arrives**: the renderer records every
            // texture of a crossing into one and flushes once. A batch apiece would be measuring the
            // command pool rather than the array.
            Batch batch(pool);

            const std::size_t before = Testing::getAllocationCount();
            for (std::uint32_t slot = 1; slot <= measured; ++slot)
                arrive(batch, slot);
            const std::size_t after = Testing::getAllocationCount();

            batch.flush();

            EXPECT_LE(after - before, budgetPerTexture * measured)
                << (after - before) << " allocations across " << measured << " arrivals, "
                << static_cast<double>(after - before) / measured << " apiece";

            EXPECT_EQ(array.getHeld().mCount, measured + 1);
        }
    }
}
