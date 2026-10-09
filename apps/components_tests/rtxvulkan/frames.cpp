#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Matrixf>
#include <osg/Vec2d>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/guirenderer.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/gbuffer.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtx/world/skycontent.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/timeline.hpp>
#include <components/rtxvulkan/scene/sceneacceleration.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>
#include <components/sky/skyclock.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sSize = 64;
        constexpr std::uint32_t sEveryPixel = sSize * sSize;

        Shaders::VisibilityConstants ahead()
        {
            return Testing::makeCamera(osg::Vec3f(), osg::Vec3f(0.0f, 100.0f, 0.0f), 60.0f, sSize, sSize, 100000.0f);
        }

        /// Two frames in flight, and what each of them read.
        ///
        /// **What is under test is that a frame keeps the world it was given.** The CPU places the
        /// next frame while the device draws this one, so the tables a frame traces have to be the
        /// ones it was placed with and not the ones the placement after overwrote — which is why
        /// every test here places and draws several frames before it asks about any of them.
        class RtxFramesTest : public Testing::RendererTest
        {
        protected:
            void SetUp() override
            {
                Testing::RendererTest::SetUp();
                mRenderer.resize(sSize, sSize);

                // On a skin of one bone, so the same wall can be moved two ways: by its instance
                // and by its pose. Its bind pose is at two hundred.
                mWall = Testing::addOneBoneBody(
                    mScene, MeshArrays{ .mPositions = Testing::wallAt(200.0f), .mIndices = Testing::sQuadIndices })
                            .mMesh;
                mInstance = mScene.addInstance(MeshInstance{ .mMesh = mWall });
                Testing::poseByOneBone(mScene, mWall, osg::Matrixf::identity());
                mRenderer.setScene(Rtx::SceneSlot::world(), mScene, {});
            }

            /// Moves the wall by its instance and hands the placement over, which goes through the
            /// instance rows and the top level.
            void moveTo(float away)
            {
                mScene.placements().move(mInstance, osg::Matrixf::translate(0.0f, away - 200.0f, 0.0f));
                mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
            }

            /// Moves the wall by its pose instead, which is what a skinned body does and goes
            /// through the skinning pass and the refit's positions.
            void deformTo(float away)
            {
                mScene.clearPlacement();
                Testing::poseByOneBone(mScene, mWall, osg::Matrixf::translate(0.0f, away - 200.0f, 0.0f));
                mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
            }

            std::uint32_t finishedHits()
            {
                const std::optional<FrameResult> result = mRenderer.finishFrame();
                EXPECT_TRUE(result.has_value()) << "a frame was in flight and none came back";
                return result.has_value() ? result->mHits : ~0u;
            }

            SceneDesc mScene;
            Index mWall = 0;
            Index mInstance = 0;
        };

        /// Nothing in flight is nothing to finish, and a frame finished once is finished.
        TEST_F(RtxFramesTest, aFrameComesBackOnceAndInTheOrderItWasDrawn)
        {
            EXPECT_FALSE(mRenderer.finishFrame().has_value()) << "nothing was drawn and something came back";

            // Placed and drawn twice over before either is asked about: the second placement writes
            // the other copy of the tables, and the first frame's trace still reads its own.
            mRenderer.renderFrame(ahead(), FrameOptions{});
            moveTo(-1000.0f);
            mRenderer.renderFrame(ahead(), FrameOptions{});

            EXPECT_EQ(finishedHits(), sEveryPixel) << "the first frame read the second frame's placement";
            EXPECT_EQ(finishedHits(), 0u) << "the second frame read the first frame's placement";
            EXPECT_FALSE(mRenderer.finishFrame().has_value()) << "a frame came back twice";
        }

        /// A cell arriving while a frame is in flight leaves that frame the world it was placed in.
        ///
        /// **What the wait in `extendScene` is for, driven rather than assumed.** An arrival appends
        /// geometry and grows every table a frame in flight may be reading, so the renderer drains
        /// the ring before it extends. This is the case that says whether it must: the suite runs
        /// under the layers' synchronization validation, so a hazard between what the arrival writes
        /// and what the frame in flight traces is reported rather than left to chance.
        ///
        /// **A hundred rigged meshes and not one, because the count is what makes a table move.**
        /// A block is only ever appended to, so one arrival proves nothing about it; a bind table,
        /// a rig's runs and the instance rows are remade by `growTo`, which doubles — so a hundred
        /// crosses several of those and each is made again while a frame still reads the one it
        /// displaced. That is the shape a cell crossing has.
        TEST_F(RtxFramesTest, aCellArrivingWhileAFrameIsInFlightLeavesThatFrameItsOwnWorld)
        {
            mRenderer.renderFrame(ahead(), FrameOptions{});

            // Behind the camera, so what the second frame sees is decided by the wall that walks
            // away rather than by a hundred quads landing over it.
            for (int at = 0; at < 100; ++at)
            {
                const Index arrived = Testing::addOneBoneBody(
                    mScene, MeshArrays{ .mPositions = Testing::wallAt(-1000.0f), .mIndices = Testing::sQuadIndices })
                                          .mMesh;
                mScene.addInstance(MeshInstance{ .mMesh = arrived });
                Testing::poseByOneBone(mScene, arrived, osg::Matrixf::identity());
            }

            mScene.placements().move(mInstance, osg::Matrixf::translate(0.0f, -1000.0f, 0.0f));
            mRenderer.extendScene(Rtx::SceneSlot::world(), mScene, {});
            mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);

            mRenderer.renderFrame(ahead(), FrameOptions{});

            EXPECT_EQ(finishedHits(), sEveryPixel) << "the frame in flight lost its wall to the arrival";
            EXPECT_EQ(finishedHits(), 0u) << "the frame after the arrival kept the wall the arrival moved";
        }

        /// A third frame waits for the first, whose slot it takes, and the first still reports.
        ///
        /// **Whichever call did the waiting, the report belongs to the frame.** The ring drains
        /// itself to make room, and a caller asking once a frame is answered once a frame however
        /// many of them that drain accounted for.
        TEST_F(RtxFramesTest, aFrameTheRingDrainedToMakeRoomStillReports)
        {
            // Placed before every frame, the first included, so the three record the same zones.
            moveTo(200.0f);
            mRenderer.renderFrame(ahead(), FrameOptions{});
            moveTo(-1000.0f);
            mRenderer.renderFrame(ahead(), FrameOptions{});
            moveTo(200.0f);
            mRenderer.renderFrame(ahead(), FrameOptions{});

            // The third placement wrote the copy the first frame traced, and could only do so once
            // the first frame had finished — which is the drain, and the frame it accounted for.
            const std::optional<FrameResult> first = mRenderer.finishFrame();
            const std::optional<FrameResult> second = mRenderer.finishFrame();
            const std::optional<FrameResult> third = mRenderer.finishFrame();
            ASSERT_TRUE(first.has_value() && second.has_value() && third.has_value())
                << "three frames were in flight and fewer came back";
            EXPECT_EQ(first->mHits, sEveryPixel) << "the wall the first frame was drawn against";
            EXPECT_EQ(second->mHits, 0u) << "the second, with the wall moved behind the eye";
            EXPECT_EQ(third->mHits, sEveryPixel) << "the third, with it moved back";
            EXPECT_FALSE(mRenderer.finishFrame().has_value()) << "a frame reported twice";

            // The zones as well as the count: the third frame took the first one's slot and began
            // its timer before the first report was read, and the report is what its frame measured
            // and not what the timer holds now.
            EXPECT_EQ(first->mGpu.spans().size(), third->mGpu.spans().size())
                << "the drained frame's zones went with its slot";
        }

        /// What the game calls before each placement: it waits only where the ring is full, so the
        /// frame behind stays on the device while the next is placed, and reports the frame before.
        TEST_F(RtxFramesTest, collectFrameWaitsOnlyWhereTheRingIsFull)
        {
            EXPECT_FALSE(mRenderer.collectFrame().has_value()) << "nothing was drawn and something came back";

            mRenderer.renderFrame(ahead(), FrameOptions{});
            EXPECT_FALSE(mRenderer.collectFrame().has_value())
                << "one frame in flight is room for another, and it was waited out";

            moveTo(-1000.0f);
            mRenderer.renderFrame(ahead(), FrameOptions{});

            // Two in flight is none to spare: the first is waited out and reported, the second stays.
            const std::optional<FrameResult> first = mRenderer.collectFrame();
            ASSERT_TRUE(first.has_value()) << "the ring was full and nothing was finished";
            EXPECT_EQ(first->mHits, sEveryPixel) << "the first frame, or the second reported first";
            EXPECT_FALSE(mRenderer.collectFrame().has_value()) << "the frame behind was waited out with room to spare";

            EXPECT_EQ(finishedHits(), 0u) << "the second frame, or the first reported twice";
            EXPECT_FALSE(mRenderer.finishFrame().has_value()) << "a frame reported twice";
        }

        /// A caller that stops collecting loses the reports that have stopped being true.
        ///
        /// **A report is a span into its frame's own timer**, good until that slot comes round and
        /// resolves again — `sFrameSlots` finishes away. Holding one past that would hand back the
        /// zones of a later frame, so the oldest goes instead. Five frames drawn and nothing asked
        /// for in between is one more finish than the ring can answer for, and the first frame is
        /// the one it cannot.
        TEST_F(RtxFramesTest, aReportIsDroppedRatherThanHeldPastTheFrameItDescribes)
        {
            for (int at = 0; at < 5; ++at)
            {
                if (at > 0)
                    moveTo(at % 2 == 0 ? 200.0f : -1000.0f);

                mRenderer.renderFrame(ahead(), FrameOptions{});
            }

            // The first frame's wall was in front of the eye; what comes back starts at the second.
            EXPECT_EQ(finishedHits(), 0u) << "the second frame, or the first held past its timer";
            EXPECT_EQ(finishedHits(), sEveryPixel);
            EXPECT_EQ(finishedHits(), 0u);
            EXPECT_EQ(finishedHits(), sEveryPixel);
            EXPECT_FALSE(mRenderer.finishFrame().has_value()) << "five frames answered five times";
        }

        /// Several placements before a trace are one frame, and the trace reads the last of them.
        ///
        /// **A frame the ring counts is a frame the caller asked for.** A cell crossing hands the
        /// scene over twice — once for what arrived and once for the walk behind it — and the game
        /// walks its precipitation beside its world. A placement past the first that closed the frame
        /// would submit an empty one in its place, spending a slot on a frame that drew nothing and
        /// handing its nought hits back as though they were the picture's.
        TEST_F(RtxFramesTest, severalPlacementsBeforeATraceAreOneFrame)
        {
            moveTo(-1000.0f);
            moveTo(200.0f);
            mRenderer.renderFrame(ahead(), FrameOptions{});

            EXPECT_EQ(finishedHits(), sEveryPixel) << "the trace read a placement other than the last";
            EXPECT_FALSE(mRenderer.finishFrame().has_value()) << "a placement came back as a frame of its own";
        }

        /// A picture inside the interface adds nothing to the frame's count, wherever between two
        /// frames it is traced.
        ///
        /// The picture is of the same wall from the same eye, so counted it would double the hits
        /// of whichever frame's buffer it landed in.
        TEST_F(RtxFramesTest, aPictureInsideTheInterfaceIsNotCountedWithTheFrame)
        {
            const GuiSlot texture = mRenderer.addGuiTexture(sSize, sSize);

            mRenderer.renderFrame(ahead(), FrameOptions{});
            mRenderer.traceGuiTexture(texture, ahead(), GuiTraceOptions{});
            mRenderer.renderFrame(ahead(), FrameOptions{});

            EXPECT_EQ(finishedHits(), sEveryPixel) << "the frame before the picture";
            EXPECT_EQ(finishedHits(), sEveryPixel) << "the frame after it";

            mRenderer.dropGuiTexture(texture);
        }

        /// A picture's copy arrives with the frame that carried it and never sooner, and a drain
        /// lands it at once.
        ///
        /// The trace rides the next submit, which is the frame after it; the copy is readable once
        /// that frame has been finished — two frames on, on the game's own cadence — and
        /// `finishGuiTraces` is the harness's way of not waiting for that.
        TEST_F(RtxFramesTest, aPicturesCopyArrivesWithTheFrameThatCarriedIt)
        {
            const GuiSlot texture = mRenderer.addGuiTexture(sSize, sSize);
            std::vector<std::uint8_t> copy(std::size_t{ sSize } * sSize * 4);

            mRenderer.traceGuiTexture(texture, ahead(), GuiTraceOptions{ .mReadBack = true });
            EXPECT_FALSE(mRenderer.takeGuiCopy(texture, copy)) << "recorded and carried by nothing yet";

            mRenderer.renderFrame(ahead(), FrameOptions{});
            EXPECT_FALSE(mRenderer.takeGuiCopy(texture, copy)) << "carried, and the frame is in flight";

            mRenderer.renderFrame(ahead(), FrameOptions{});
            EXPECT_EQ(finishedHits(), sEveryPixel);
            EXPECT_TRUE(mRenderer.takeGuiCopy(texture, copy)) << "the frame that carried it is finished";
            EXPECT_EQ(copy[3], 255) << "the wall, opaque, at the first pixel";

            mRenderer.traceGuiTexture(texture, ahead(), GuiTraceOptions{ .mReadBack = true });
            EXPECT_FALSE(mRenderer.takeGuiCopy(texture, copy)) << "a new trace is a new wait";
            mRenderer.finishGuiTraces();
            EXPECT_TRUE(mRenderer.takeGuiCopy(texture, copy)) << "drained";

            mRenderer.dropGuiTexture(texture);
        }

        /// A row appended while one copy of the rows was in flight reaches the other copy whole.
        ///
        /// **The copy that was not placed when the scene grew is smaller than the mirror**, and its
        /// next placement owes it the appended row — at an offset past its end. The wall that
        /// arrives is behind the eye and the first wall is moved behind it, so a copy still holding
        /// the first wall's old row, or built from past its end, hits something.
        TEST_F(RtxFramesTest, aRowAppendedWhileTheOtherCopyWasInFlightReachesIt)
        {
            const Index arrived = mScene.addInstance(
                MeshInstance{ .mTransform = osg::Matrixf::translate(0.0f, -1200.0f, 0.0f), .mMesh = mWall });
            mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
            mRenderer.renderFrame(ahead(), FrameOptions{});

            // Placed into the copy the first frame is not reading, while that frame is in flight.
            moveTo(-1000.0f);
            mRenderer.renderFrame(ahead(), FrameOptions{});

            // Collected here and not at the end: the placement below writes the copy the first
            // frame read, and waits it out — and a frame nothing collected before its copy comes
            // round again is reclaimed with its numbers.
            EXPECT_EQ(finishedHits(), sEveryPixel) << "the first wall, before anything moved";

            mScene.placements().move(arrived, osg::Matrixf::identity());
            mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
            mRenderer.renderFrame(ahead(), FrameOptions{});

            EXPECT_EQ(finishedHits(), 0u) << "both walls behind the eye, in the copy that grew late";
            EXPECT_EQ(finishedHits(), sEveryPixel) << "the wall that arrived, moved in front";
            EXPECT_FALSE(mRenderer.finishFrame().has_value());
        }

        /// A mesh whose vertices changed keeps its old ones for the frame still tracing them.
        ///
        /// The transform path above goes through the instance rows; this one goes through the
        /// refit's positions, which are the other table a placement writes and a frame reads.
        TEST_F(RtxFramesTest, aDeformedMeshKeepsItsOldVerticesForTheFrameStillTracingThem)
        {
            deformTo(400.0f);
            mRenderer.renderFrame(ahead(), FrameOptions{});
            deformTo(-1000.0f);
            mRenderer.renderFrame(ahead(), FrameOptions{});
            deformTo(400.0f);
            mRenderer.renderFrame(ahead(), FrameOptions{});

            EXPECT_EQ(finishedHits(), sEveryPixel) << "the first pose, in front of the eye";
            EXPECT_EQ(finishedHits(), 0u) << "the second, moved behind it";
            EXPECT_EQ(finishedHits(), sEveryPixel) << "the third, moved back";
            EXPECT_FALSE(mRenderer.finishFrame().has_value());
        }

        /// A surface moved by its pose reprojects exactly as the same surface moved by its instance:
        /// the motion vector knows where the triangle stood, and not only where the body did.
        ///
        /// **The other copy of the poses is last frame's, and this is what reads it.** A frame
        /// traces the copy its placement synced; the copy it did not trace was synced the frame
        /// before, so it holds every pose as of then — `GpuTables::mPreviousPoseBlocks` — and a
        /// hit on a body takes its own step off the pair. Four units along +x at two hundred units
        /// off is the 1.1085 pixels `aMotionVectorSaysWhereItsSurfaceWasAndNotWhereTheWorldIs`
        /// derives for a stepping camera, with the sign the surface's own: it went right, so the
        /// point now under the centre pixel was to its left. Read twice, so the sign is not a
        /// guess about the instance path, and once more on a frame nothing moved, which is the
        /// copies agreeing.
        ///
        /// **Ten units toward the eye moves the distance and not the screen.** The centre ray leans
        /// `a = tan 30° / 64` off the axis on two sides, so the point it finds at 190 units ahead
        /// is `190 sqrt(1 + 2a²)` = 190.0155 off and stood `sqrt(200² + 2 (190a)²)` = 200.0147 off
        /// the eye a frame ago: 9.9992 farther.
        TEST_F(RtxFramesTest, aPoseMovesAMotionVectorAsAnInstanceDoes)
        {
            constexpr std::size_t centre = std::size_t{ sSize / 2 } * sSize + sSize / 2;
            const auto centreMotion = [&] {
                std::vector<float> motion;
                mRenderer.readChannel(Channel::Motion, motion);
                return osg::Vec3f(motion[centre * 4], motion[centre * 4 + 1], motion[centre * 4 + 2]);
            };

            struct Moved
            {
                osg::Vec3f mByInstance;
                osg::Vec3f mByPose;
            };

            // From the bind pose held two frames, so both copies of the poses hold it: by the
            // instance, as the reprojection always knew how to; then back where it was, and by the
            // pose alone: the instance stands still and the bone carries the wall as far.
            const auto movedBy = [&](const osg::Vec3f& step) {
                mScene.clearPlacement();
                Testing::poseByOneBone(mScene, mWall, osg::Matrixf::identity());
                mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
                mRenderer.renderFrame(ahead(), FrameOptions{});
                mScene.clearPlacement();
                mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
                mRenderer.renderFrame(ahead(), FrameOptions{});
                mScene.placements().move(mInstance, osg::Matrixf::translate(step));
                mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
                mRenderer.renderFrame(ahead(), FrameOptions{});
                const osg::Vec3f byInstance = centreMotion();

                mScene.placements().move(mInstance, osg::Matrixf::identity());
                mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
                mRenderer.renderFrame(ahead(), FrameOptions{});
                mScene.clearPlacement();
                Testing::poseByOneBone(mScene, mWall, osg::Matrixf::translate(step));
                mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
                mRenderer.renderFrame(ahead(), FrameOptions{});

                return Moved{ .mByInstance = byInstance, .mByPose = centreMotion() };
            };

            const Moved across = movedBy(osg::Vec3f(4.0f, 0.0f, 0.0f));
            EXPECT_NEAR(across.mByInstance.x(), -1.1085f, 0.02f)
                << "the surface went right, so the point came from the left";
            EXPECT_NEAR(across.mByInstance.y(), 0.0f, 1e-3f);

            const Moved toward = movedBy(osg::Vec3f(0.0f, -10.0f, 0.0f));
            EXPECT_NEAR(toward.mByInstance.z(), 9.9992f, 0.01f) << "the surface came ten units nearer";

            for (const Moved& moved : { across, toward })
                for (std::size_t axis = 0; axis < 3; ++axis)
                    EXPECT_NEAR(moved.mByPose[axis], moved.mByInstance[axis], 1e-3f)
                        << "axis " << axis << ": a pose and an instance moved the same step";

            // A frame on which the body did not move: the copy this frame traces and the copy it
            // did not hold the same pose, so the step is nought exactly and not a rounding.
            mScene.clearPlacement();
            mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
            mRenderer.renderFrame(ahead(), FrameOptions{});
            const osg::Vec3f still = centreMotion();
            EXPECT_EQ(still.x(), 0.0f) << "nothing moved and the vector says something did";
            EXPECT_EQ(still.y(), 0.0f);
            EXPECT_EQ(still.z(), 0.0f);

            while (mRenderer.finishFrame().has_value())
            {
            }
        }

        /// **A pose turns a body's shading normal, read off the copy its frame posed, and a standing
        /// mesh beside it keeps its own.** A wall on one bone whose vertex normals lean off its
        /// plane, `(1, -1, 0)`, turned a quarter about the view axis by its bone: in
        /// OpenSceneGraph's row-vector convention a quarter turn about +y takes x to -z, so the
        /// normal reads `(0, -1, -1) / √2`. Its id unmoved would read the shared blocks' run nothing
        /// wrote, and the plane, `(0, -1, 0)`; unposed, `(1, -1, 0) / √2`. Two frames, one on each
        /// copy. Then the bone carries it a thousand units back, behind the eye, and the standing
        /// wall behind it shows its own lean, `(-1, -1, 0) / √2`, out of the shared blocks. Within
        /// 1e-3 a component: the channel's code (`packSurfaceNormal`) holds a diagonal to its own
        /// precision and not exactly.
        TEST_F(RtxFramesTest, aPoseTurnsABodysShadingNormalOnEveryCopy)
        {
            const osg::Vec3f leaning(1.0f, -1.0f, 0.0f);
            const std::array normals{ leaning, leaning, leaning, leaning };

            SceneDesc scene;
            const Index body = Testing::addOneBoneBody(scene,
                MeshArrays{
                    .mPositions = Testing::wallAt(200.0f), .mNormals = normals, .mIndices = Testing::sQuadIndices })
                                   .mMesh;
            scene.addInstance(MeshInstance{ .mMesh = body });

            const osg::Vec3f back(-1.0f, -1.0f, 0.0f);
            const std::array backward{ back, back, back, back };
            scene.addInstance(MeshInstance{ .mMesh = scene.addMesh(MeshArrays{ .mPositions = Testing::wallAt(400.0f),
                                                .mNormals = backward,
                                                .mIndices = Testing::sQuadIndices }) });

            Testing::poseByOneBone(scene, body, osg::Matrixf::identity());
            mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});

            // Two floats a pixel: the normal's code, then the distance.
            constexpr std::size_t centre = (std::size_t{ sSize / 2 } * sSize + sSize / 2) * 2;
            const auto centreNormalPosedBy = [&](const osg::Matrixf& bone) {
                scene.clearPlacement();
                Testing::poseByOneBone(scene, body, bone);
                mRenderer.placeScene(Rtx::SceneSlot::world(), scene);
                mRenderer.renderFrame(ahead(), FrameOptions{});

                std::vector<float> surface;
                mRenderer.readChannel(Channel::Surface, surface);
                return Shaders::unpackSurfaceNormal(surface[centre]);
            };

            const osg::Matrixf turn = osg::Matrixf::rotate(osg::PI_2f, osg::Vec3f(0.0f, 1.0f, 0.0f));
            const osg::Vec3f turned = osg::Vec3f(0.0f, -1.0f, -1.0f) / std::sqrt(2.0f);
            for (int frame = 0; frame < 2; ++frame)
            {
                const osg::Vec3f read = centreNormalPosedBy(turn);
                for (int axis = 0; axis < 3; ++axis)
                    EXPECT_NEAR(read[axis], turned[axis], 1e-3f) << "frame " << frame << ", axis " << axis;
            }

            const osg::Vec3f standing = centreNormalPosedBy(turn * osg::Matrixf::translate(0.0f, -1000.0f, 0.0f));
            const osg::Vec3f leaningBack = back / std::sqrt(2.0f);
            for (int axis = 0; axis < 3; ++axis)
                EXPECT_NEAR(standing[axis], leaningBack[axis], 1e-3f) << "the standing wall, axis " << axis;

            while (mRenderer.finishFrame().has_value())
            {
            }
        }

        /// **The frame's reserve is the running mode's, and the world is built again where it moves.**
        /// Native upscaling keeps the upscaler's images beside a trace of the same extent, so its
        /// reserve stands over the mode with none; under each, what the targets hold stays within
        /// what is kept for them. A move of the reserve releases the world, so the next hand-over
        /// stands every texture against the new room (`SceneUploader` builds a slot that holds
        /// nothing); a change that leaves the reserve where it was keeps it. A world let go gives
        /// back every byte of every use it took, so a second release leaves what the first did. And
        /// a run that sums, as this one may (`RadianceWidth::Summed`), fills its reserve to the byte
        /// once a frame sums: the running sum and the deep picture are the last of what it keeps
        /// room for, and nothing is kept that no frame makes.
        TEST_F(RtxFramesTest, theReserveIsTheRunningModesAndAMoveBuildsTheWorldAgain)
        {
            const MemoryAllocator& memory = mRenderer.getDevice().getMemory();
            const std::uint32_t heap = memory.getVideoHeap();
            const auto worldHeld = [&] { return mRenderer.describeHeld(Rtx::SceneSlot::world()).mIdentity != 0; };
            const auto contentHeld = [&] {
                std::array<VkDeviceSize, sMemoryUses> held{};
                for (std::size_t use = static_cast<std::size_t>(MemoryUse::Essential); use < sMemoryUses; ++use)
                    held[use] = memory.getHeld(heap, static_cast<MemoryUse>(use));
                return held;
            };
            const auto runAt = [&](Upscale mode) {
                mRenderer.setUpscale(mode);
                EXPECT_LE(memory.getHeld(heap, MemoryUse::Frame), memory.getFrameReserve())
                    << "targets past their reserve under " << sUpscaleNames.name(mode);
                return memory.getFrameReserve();
            };

            ASSERT_TRUE(worldHeld());
            const VkDeviceSize alone = runAt(Upscale::Off);
            ASSERT_TRUE(worldHeld()) << "a mode the renderer ran already moved nothing";

            const VkDeviceSize upscaled = runAt(Upscale::Native);
            EXPECT_GT(upscaled, alone) << "the upscaler's images were not kept room for";
            EXPECT_FALSE(worldHeld()) << "a reserve that grew kept content chosen against less room";
            const std::array<VkDeviceSize, sMemoryUses> released = contentHeld();

            mRenderer.setScene(Rtx::SceneSlot::world(), mScene, {});
            ASSERT_GT(contentHeld()[static_cast<std::size_t>(MemoryUse::Structure)],
                released[static_cast<std::size_t>(MemoryUse::Structure)])
                << "the world built again holds no structure, so the release below proves nothing";
            EXPECT_EQ(runAt(Upscale::Native), upscaled);
            EXPECT_TRUE(worldHeld()) << "a reserve that stayed where it was built the world again";

            EXPECT_EQ(runAt(Upscale::Off), alone) << "the reserve did not follow the mode back";
            EXPECT_FALSE(worldHeld()) << "a reserve that shrank kept content held to less room than it has";
            EXPECT_EQ(contentHeld(), released) << "a world let go kept memory past its release";

            ASSERT_EQ(mRenderer.getProfile().mRadianceWidth, RadianceWidth::Summed);
            mRenderer.setScene(Rtx::SceneSlot::world(), mScene, {});
            mRenderer.renderFrame(ahead(), FrameOptions{ .mAccumulate = 1 });
            ASSERT_TRUE(mRenderer.finishFrame().has_value());
            EXPECT_EQ(memory.getHeld(heap, MemoryUse::Frame), memory.getFrameReserve())
                << "the reserve and what a summing frame holds differ";
        }

        /// A refitted structure is built whole again on a rota: the posed body built longest ago,
        /// on every placement that poses anything, one a placement, and never one built whole on
        /// its own arrival.
        ///
        /// **Counted through the scene's report and driven through the placements a frame makes**,
        /// so the rule is read where a run reads it. The wall alone is built whole on every posed
        /// placement after the one it arrived on. A second body arriving is not built again on the
        /// placement that brings it; the two then share the rota, each placement building exactly
        /// one of them. A placement that poses neither is not a tick of the rota at all.
        TEST_F(RtxFramesTest, aRefittedStructureIsBuiltWholeAgainOnARota)
        {
            // The fixture's `setScene` left the wall among the arrivals, as the uploader would not
            // have: an extension below must bring the second body alone, or it builds the wall
            // again and notes it built.
            mScene.clearArrivals();

            const auto rebuilt = [&] { return mRenderer.getSceneStats().mRebuilt; };

            // Past the placement the wall arrived for: a pose that moves, because one the wall
            // already holds poses nothing and is no tick of the rota.
            deformTo(199.0f);
            const std::uint64_t alone = rebuilt();
            for (std::uint64_t placement = 1; placement <= 3; ++placement)
            {
                deformTo(200.0f + static_cast<float>(placement));
                EXPECT_EQ(rebuilt(), alone + placement) << "the wall alone, built whole on every posed placement";
            }

            // Frames, so the placements above are drawn and the rebuilt structure is traced: a
            // structure built whole in place of a refit is the same wall to a ray.
            mRenderer.renderFrame(ahead(), FrameOptions{});
            EXPECT_EQ(finishedHits(), sEveryPixel);

            // A second body, arriving now and posed alone on the placement that brings it: its
            // arrival built it whole for that placement, so the placement builds nothing whole.
            const Index second = Testing::addOneBoneBody(
                mScene, MeshArrays{ .mPositions = Testing::wallAt(300.0f), .mIndices = Testing::sQuadIndices })
                                     .mMesh;
            mScene.addInstance(MeshInstance{ .mMesh = second });
            mScene.clearPlacement();
            Testing::poseByOneBone(mScene, second, osg::Matrixf::identity());
            mRenderer.extendScene(Rtx::SceneSlot::world(), mScene, {});
            mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
            mScene.clearArrivals();
            const std::uint64_t arrived = rebuilt();
            EXPECT_EQ(arrived, alone + 3) << "an arrival is built again on the placement that brings it";

            const auto deformBoth = [&](float x) {
                mScene.clearPlacement();
                Testing::poseByOneBone(mScene, mWall, osg::Matrixf::translate(x, 0.0f, 0.0f));
                Testing::poseByOneBone(mScene, second, osg::Matrixf::translate(-x, 0.0f, 0.0f));
                mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
            };
            for (std::uint64_t placement = 1; placement <= 4; ++placement)
            {
                deformBoth(static_cast<float>(placement % 3));
                EXPECT_EQ(rebuilt(), arrived + placement) << "one built whole a placement, and never two";
            }

            // And a placement that poses neither is not a placement of the rota at all, however
            // long it has been.
            mScene.clearPlacement();
            mRenderer.placeScene(Rtx::SceneSlot::world(), mScene);
            EXPECT_EQ(rebuilt(), arrived + 4);

            // A frame over the placements above, because the zones a placement opens are the next
            // frame's report: drawn here they are this test's, and the shared renderer's next
            // frame is its own again.
            mRenderer.renderFrame(ahead(), FrameOptions{});
            EXPECT_EQ(finishedHits(), sEveryPixel);
        }

        /// A picture still deferred traces the copy it was placed with, however many placements of
        /// its scene follow it in the frame.
        ///
        /// **Content, not memory.** A picture inside the interface is a deferred batch: its
        /// placement built the top level from the rows of the moment and its trace reads that
        /// copy's instance table, both carried by the next submit. A third placement of the scene
        /// in the same frame writes that copy again from the host, ahead of the submit — no race,
        /// because nothing is on the queue yet, and so nothing the tables' stamps would wait for.
        /// The picture would then trace the first placement's top level against the third's rows,
        /// and here read the wall's opacity as the fade the third placement wrote and see through
        /// it. So a placement into a copy whose picture is still deferred carries the picture
        /// first, and a placement into the other copy does not.
        TEST_F(RtxFramesTest, aPlacementIntoTheCopyADeferredPictureReadsCarriesThePictureFirst)
        {
            // A scene of its own, because a picture's placements are deferred like its trace: the
            // world's placement would carry the picture on its own submit.
            const SceneSlot doll = mRenderer.addViewScene();
            SceneDesc scene;
            const Index wall = Testing::addQuadMesh(scene, Testing::wallAt(200.0f));
            const Index standing = scene.addInstance(MeshInstance{ .mMesh = wall });
            mRenderer.setScene(doll, scene, {});

            const GuiSlot texture = mRenderer.addGuiTexture(sSize, sSize);
            std::vector<std::uint8_t> copy(std::size_t{ sSize } * sSize * 4);

            // The picture, of the copy the load wrote, with the wall whole. Over nothing, so a
            // pixel the wall does not cover is the one number that says so.
            Shaders::VisibilityConstants camera = ahead();
            camera.mTransparentBackground = 1;
            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{ .mScene = doll, .mReadBack = true });

            // Faded out, and placed twice: into the other copy, which leaves the picture deferred,
            // and then into the picture's own.
            scene.placements().fade(standing, 0.0f);
            mRenderer.placeScene(doll, scene);
            EXPECT_FALSE(mRenderer.takeGuiCopy(texture, copy)) << "a placement into the other copy carried it";
            mRenderer.placeScene(doll, scene);

            mRenderer.finishGuiTraces();
            ASSERT_TRUE(mRenderer.takeGuiCopy(texture, copy));
            EXPECT_EQ(copy[3], 255) << "the picture saw through the fade a later placement wrote into its copy";

            mRenderer.dropGuiTexture(texture);
            mRenderer.dropViewScene(doll);
        }

        /// A picture's scene is built on a batch that rides the next submit, as an arrival is:
        /// opening one submits nothing and so waits for nothing, and opening another in its slot
        /// buries the first, so a picture of it recorded and not yet carried still comes back
        /// whole. A drain here idled the device every time the inventory or the race menu opened.
        TEST_F(RtxFramesTest, aPictureSceneOpensWithoutASubmitAndKeepsAPictureOfTheOneItReplaced)
        {
            const SceneSlot doll = mRenderer.addViewScene();
            SceneDesc scene;
            const Index wall = Testing::addQuadMesh(scene, Testing::wallAt(200.0f));
            scene.addInstance(MeshInstance{ .mMesh = wall });

            const Timeline& timeline = mRenderer.getDevice().getTimeline();
            const std::uint64_t opened = timeline.getNext();
            mRenderer.setScene(doll, scene, {});
            EXPECT_EQ(timeline.getNext(), opened) << "opening a picture's scene submitted";

            const GuiSlot texture = mRenderer.addGuiTexture(sSize, sSize);
            std::vector<std::uint8_t> copy(std::size_t{ sSize } * sSize * 4);

            // Over nothing, so a pixel the wall does not cover is the one number that says so.
            Shaders::VisibilityConstants camera = ahead();
            camera.mTransparentBackground = 1;
            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{ .mScene = doll, .mReadBack = true });

            // An empty scene in its place, with the picture of the wall still deferred.
            const std::uint64_t replaced = timeline.getNext();
            mRenderer.setScene(doll, SceneDesc{}, {});
            EXPECT_EQ(timeline.getNext(), replaced) << "replacing a picture's scene submitted";
            EXPECT_FALSE(mRenderer.takeGuiCopy(texture, copy)) << "replacing the scene carried the picture";

            mRenderer.finishGuiTraces();
            ASSERT_TRUE(mRenderer.takeGuiCopy(texture, copy));
            EXPECT_EQ(copy[3], 255) << "the picture was traced against the scene that replaced it";

            // And a picture taken now is of the empty scene.
            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{ .mScene = doll, .mReadBack = true });
            mRenderer.finishGuiTraces();
            ASSERT_TRUE(mRenderer.takeGuiCopy(texture, copy));
            EXPECT_EQ(copy[3], 0) << "the replacement was not what the next picture traced";

            mRenderer.dropGuiTexture(texture);
            mRenderer.dropViewScene(doll);
        }

        /// A picture's scene given back is not drained: the picture recorded against it and not
        /// yet carried still rides the next submit and still comes back whole, the frames drawn
        /// after the drop are placed and traced without a wait, and the layers see nothing read
        /// after it went. A drain here idled the device every time the inventory closed.
        TEST_F(RtxFramesTest, aViewSceneGivenBackWithAPictureStillDeferredIsCarriedAndThenLetGo)
        {
            const SceneSlot doll = mRenderer.addViewScene();
            SceneDesc scene;
            const Index wall = Testing::addQuadMesh(scene, Testing::wallAt(200.0f));
            scene.addInstance(MeshInstance{ .mMesh = wall });
            mRenderer.setScene(doll, scene, {});

            const GuiSlot texture = mRenderer.addGuiTexture(sSize, sSize);
            std::vector<std::uint8_t> copy(std::size_t{ sSize } * sSize * 4);

            Shaders::VisibilityConstants camera = ahead();
            camera.mTransparentBackground = 1;
            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{ .mScene = doll, .mReadBack = true });

            // Given back with the picture still deferred, and the world drawn on as if nothing
            // happened: three frames, which is more than the ring holds, so the scene's submit has
            // been waited out by the end of them and the scene has gone.
            mRenderer.dropViewScene(doll);
            for (int frame = 0; frame < 3; ++frame)
            {
                moveTo(-1000.0f + 100.0f * static_cast<float>(frame));
                mRenderer.renderFrame(ahead(), FrameOptions{});
                mRenderer.collectFrame();
            }
            mRenderer.finishGuiTraces();

            ASSERT_TRUE(mRenderer.takeGuiCopy(texture, copy)) << "the picture the drop left deferred never landed";
            EXPECT_EQ(copy[3], 255) << "the picture was traced against a scene that had gone";

            while (mRenderer.finishFrame().has_value())
            {
            }

            mRenderer.dropGuiTexture(texture);
        }

        /// **A frame a host asks for is its viewpoint with its world described over it, and its air
        /// carried on**: the block the renderer traced and the carry it hands back are what
        /// `constantsFor` and `describeWorld` make of the request — the one writer of each field —
        /// and `holdAir` stands the carry where a harness recorded it. Outdoors, so the air drifts
        /// along the deck's heading, which a sky with no sheet reads as north.
        TEST_F(RtxFramesTest, aRequestIsTracedAsItsWorldDescribesItAndCarriesTheAirOn)
        {
            const SkyContent sky;
            const auto requestAt = [&](double skySeconds) {
                FrameRequest request{
                    .mView = makeCameraFromView(osg::Matrixf::lookAt(osg::Vec3f(), osg::Vec3f(0.0f, 100.0f, 0.0f),
                                                    osg::Vec3f(0.0f, 0.0f, 1.0f)),
                        60.0f, sSize, sSize, sNearPlane, 100000.0f)
                                 .value(),
                    .mRayMask = Shaders::MASK_STATIC,
                    .mLamps = false,
                    .mSampleFrame = 7,
                    .mWorld = {},
                    .mSky = &sky,
                    .mOptions = {},
                };
                request.mWorld.mOutdoors = true;
                request.mWorld.mDaylight.mFog.mWind = 0.5f;
                request.mWorld.mSkySeconds = skySeconds;
                return request;
            };

            // From a carry of nought at nought, whatever a frame before this one left.
            FogDrift drift;
            drift.hold(osg::Vec2d(), 0.0);
            mRenderer.holdAir(AirClock{ .mSky = Sky::SkyClock{ .mSeconds = 0.0 }, .mCarried = osg::Vec2d() });

            for (const double seconds : { 0.0, 10.0 })
            {
                const FrameRequest request = requestAt(seconds);
                const FrameTraced traced = mRenderer.renderFrame(request);
                finishedHits();

                Shaders::VisibilityConstants expected = constantsFor(request.mView);
                expected.mRayMask = Shaders::MASK_STATIC;
                expected.mNoLamps = 1;
                expected.mFrame = 7;
                WorldOptions options;
                describeWorld(request.mWorld, sky, drift, expected, options);
                EXPECT_EQ(std::memcmp(&traced.mConstants, &expected, sizeof(expected)), 0) << "at " << seconds << " s";
                EXPECT_EQ(traced.mCarried, drift.get()) << "at " << seconds << " s";
            }
            ASSERT_NE(drift.get(), osg::Vec2d()) << "the air was carried, or the carry proves nothing";

            // Held, the next frame at the held clock draws the held carry and moves it nothing.
            const osg::Vec2d held(123.0, -45.0);
            mRenderer.holdAir(AirClock{ .mSky = Sky::SkyClock{ .mSeconds = 20.0 }, .mCarried = held });
            EXPECT_EQ(mRenderer.renderFrame(requestAt(20.0)).mCarried, held);
            finishedHits();
        }
    }
}
