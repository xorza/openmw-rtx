#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Matrixf>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <apps/components_tests/rtx/support/device/readback.hpp>
#include <apps/components_tests/rtx/support/displaycurve.hpp>
#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/guiquad.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/guirenderer.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sprite.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/gui/guitextures.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sExtent = 8;

        constexpr std::array<std::uint8_t, 4> sWhite{ 255, 255, 255, 255 };

        /// The GUI as the renderer offers it: a table of textures and one call that draws with them.
        ///
        /// **No scene, and that is the point.** A main menu and a loading screen are drawn over a
        /// frame nothing traced, so everything here has to work before `setScene` has been called
        /// even once.
        class RtxGuiDrawTest : public Testing::RendererTest
        {
        protected:
            void SetUp() override
            {
                Testing::RendererTest::SetUp();
                mRenderer.resize(sExtent, sExtent);
            }

            void TearDown() override
            {
                for (const GuiSlot texture : mHeld)
                    mRenderer.dropGuiTexture(texture);
                mHeld.clear();

                Testing::RendererTest::TearDown();
            }

            /// A one-texel texture of this colour, dropped when the test ends.
            GuiSlot makeTexel(std::array<std::uint8_t, 4> colour)
            {
                const GuiSlot texture = mRenderer.addGuiTexture(1, 1);
                mHeld.push_back(texture);
                Testing::writeTexture(mRenderer, texture, GuiRegion{ 0, 0, 1, 1 }, colour);
                return texture;
            }

            void drawQuad(GuiSlot texture, float left, float top, float right, float bottom, std::uint32_t colour)
            {
                const std::array<GuiVertex, 6> quad = Testing::makeGuiQuad(left, top, right, bottom, colour);
                const std::array<GuiBatch, 1> batches{ GuiBatch{ texture, 0, quad.size() } };
                mRenderer.drawGui(quad, batches);
            }

            /// The four bytes at a pixel of a GUI texture, row zero at the top.
            std::array<std::uint8_t, 4> inTexture(
                GuiSlot texture, std::uint32_t extent, std::uint32_t x, std::uint32_t y)
            {
                mRenderer.readGuiTexture(texture, mPixels);
                EXPECT_EQ(mPixels.size(), std::size_t{ extent } * extent * 4);

                return Testing::rgbaAt(mPixels, extent, x, y);
            }

            /// The four bytes at a pixel of the presented frame, row zero at the top.
            std::array<std::uint8_t, 4> at(std::uint32_t x, std::uint32_t y)
            {
                mRenderer.readPixels(mPixels);
                EXPECT_EQ(mPixels.size(), std::size_t{ sExtent } * sExtent * 4);

                return Testing::rgbaAt(mPixels, sExtent, x, y);
            }

            std::vector<GuiSlot> mHeld;
            std::vector<std::uint8_t> mPixels;
        };

        /// What a batch shows is its texture times its vertex colour, and nothing else.
        TEST_F(RtxGuiDrawTest, aBatchShowsItsTextureTimesItsVertexColour)
        {
            // Halves that divide exactly, so the multiply has nothing to round: 200 × 128/255 is not
            // an integer, but 200 × 1 and 100 × 1 are.
            const GuiSlot texture = makeTexel({ 200, 100, 50, 255 });

            drawQuad(texture, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(255, 255, 255, 255));

            EXPECT_EQ(at(4, 4), (std::array<std::uint8_t, 4>{ 200, 100, 50, 255 }));
        }

        /// A second call draws over what the first left, rather than over whatever the frame held.
        ///
        /// **This is what makes a GUI out of one call per frame.** MyGUI produces its batches in
        /// layer order and the renderer draws them in that order; a pass that discarded the target
        /// each time would show only the last thing drawn.
        TEST_F(RtxGuiDrawTest, aSecondDrawLandsOverTheFirst)
        {
            const GuiSlot white = makeTexel({ 255, 255, 255, 255 });

            drawQuad(white, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(0, 0, 255, 255));
            drawQuad(white, -1.0f, 1.0f, 0.0f, -1.0f, Testing::packColour(255, 0, 0, 128));

            // Exact: an alpha of 128/255 contributes 255 × 128/255 = 128 and leaves
            // 255 × 127/255 = 127 of what was there.
            EXPECT_EQ(at(1, 4), (std::array<std::uint8_t, 4>{ 128, 0, 127, 255 })) << "blended over";
            EXPECT_EQ(at(sExtent - 2, 4), (std::array<std::uint8_t, 4>{ 0, 0, 255, 255 })) << "left alone";
        }

        /// Writing a texture again changes what is drawn with it, which is the whole of what a video
        /// frame and a fog of war need.
        TEST_F(RtxGuiDrawTest, rewritingATextureChangesWhatIsDrawn)
        {
            const GuiSlot texture = makeTexel({ 255, 0, 0, 255 });

            drawQuad(texture, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(255, 255, 255, 255));
            EXPECT_EQ(at(4, 4), (std::array<std::uint8_t, 4>{ 255, 0, 0, 255 })) << "as written";

            constexpr std::array<std::uint8_t, 4> sGreen{ 0, 255, 0, 255 };
            Testing::writeTexture(mRenderer, texture, GuiRegion{ 0, 0, 1, 1 }, sGreen);

            drawQuad(texture, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(255, 255, 255, 255));
            EXPECT_EQ(at(4, 4), (std::array<std::uint8_t, 4>{ 0, 255, 0, 255 })) << "as rewritten";
        }

        /// A write of part of a texture changes that part and leaves the rest where it was.
        ///
        /// **What the world map needs and MyGUI's own interface cannot say.** Entering a cell
        /// repaints eighteen pixels square of an overlay two megabytes wide, and sending the whole
        /// of it on the frame the cell arrives is the cost this exists to remove.
        ///
        /// Read back rather than drawn: what is under test is which texels the copy landed on, and a
        /// linear sampler over a four-texel texture blends every one of them into its neighbours.
        ///
        /// **Also what says the two writes happen in the order they were asked for.** Nothing reads
        /// the texture between them, so they share a submit — and copies into one image are
        /// unordered within a submit unless something orders them.
        TEST_F(RtxGuiDrawTest, aRegionWriteChangesItsRectangleAndNothingElse)
        {
            constexpr std::uint32_t side = 4;
            const GuiSlot texture = mRenderer.addGuiTexture(side, side);
            mHeld.push_back(texture);

            std::array<std::uint8_t, side * side * 4> red{};
            for (std::size_t at = 0; at < red.size(); at += 4)
            {
                red[at] = 255;
                red[at + 3] = 255;
            }
            Testing::writeTexture(mRenderer, texture, GuiRegion{ 0, 0, side, side }, red);

            // Two texels wide and one tall, at the second column of the second row: a write that
            // ignored the offset, took it as a row count, or transposed it lands somewhere the sweep
            // below looks.
            constexpr std::array<std::uint8_t, 8> green{ 0, 255, 0, 255, 0, 255, 0, 255 };
            Testing::writeTexture(mRenderer, texture, GuiRegion{ 1, 2, 2, 1 }, green);

            for (std::uint32_t row = 0; row < side; ++row)
                for (std::uint32_t column = 0; column < side; ++column)
                {
                    const bool written = row == 2 && (column == 1 || column == 2);
                    const std::array<std::uint8_t, 4> expected = written
                        ? std::array<std::uint8_t, 4>{ 0, 255, 0, 255 }
                        : std::array<std::uint8_t, 4>{ 255, 0, 0, 255 };

                    EXPECT_EQ(inTexture(texture, side, column, row), expected)
                        << "texel " << column << ", " << row << (written ? " was not written" : " was not left alone");
                }
        }

        /// A slot given back is taken over before the table grows.
        ///
        /// **A session opens and closes menus for hours**, and every window that opens makes
        /// textures. A table that only ever grew would be a slow leak with a number on it.
        TEST_F(RtxGuiDrawTest, aSlotGivenBackIsTakenOverBeforeTheTableGrows)
        {
            const GuiSlot first = mRenderer.addGuiTexture(1, 1);
            const GuiSlot second = mRenderer.addGuiTexture(1, 1);
            EXPECT_NE(first, second);

            mRenderer.dropGuiTexture(first);

            const GuiSlot third = mRenderer.addGuiTexture(1, 1);
            EXPECT_EQ(third, first) << "the freed slot, not a new one";

            mRenderer.dropGuiTexture(second);
            mRenderer.dropGuiTexture(third);
        }

        /// A texture the table has just handed out is blank rather than whatever the memory held.
        ///
        /// **Which is a statement about when the clear runs, not only that it is asked for.** Making
        /// a texture records a clear and submits nothing; this is what says it has run by the time a
        /// draw can name the slot.
        TEST_F(RtxGuiDrawTest, aTextureIsBlankBeforeItIsWritten)
        {
            const GuiSlot texture = mRenderer.addGuiTexture(1, 1);
            mHeld.push_back(texture);

            const GuiSlot white = makeTexel({ 255, 255, 255, 255 });
            drawQuad(white, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(0, 0, 255, 255));

            // Nothing times anything is nothing: transparent black leaves the blue underneath.
            drawQuad(texture, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(255, 255, 255, 255));

            EXPECT_EQ(at(4, 4), (std::array<std::uint8_t, 4>{ 0, 0, 255, 255 }));
        }

        /// Textures made and written before anything reads one each come back holding their own.
        ///
        /// **What the staging buffer is really being asked.** The writes share a submit, so they
        /// share the buffer they are copied out of, a run apiece; three sizes rather than three of
        /// one because a run handed out at the wrong offset only shows where the lengths differ.
        /// Between them they are more than one buffer's worth, so at least one write has to submit
        /// what is pending and start the buffer again — and what was already recorded must still
        /// land.
        TEST_F(RtxGuiDrawTest, texturesWrittenBeforeAnyIsReadEachHoldTheirOwn)
        {
            struct Written
            {
                std::uint32_t mSide;
                std::array<std::uint8_t, 4> mColour;
                GuiSlot mSlot{};
            };

            // A megabyte, a kilobyte and four bytes: the largest is what the staging settles at, and
            // the two after it borrow a corner of what that left.
            std::array<Written, 3> written{
                Written{ .mSide = 512, .mColour = { 255, 0, 0, 255 } },
                Written{ .mSide = 16, .mColour = { 0, 255, 0, 255 } },
                Written{ .mSide = 1, .mColour = { 0, 0, 255, 255 } },
            };

            std::vector<std::uint8_t> rows;
            for (Written& one : written)
            {
                one.mSlot = mRenderer.addGuiTexture(one.mSide, one.mSide);
                mHeld.push_back(one.mSlot);

                rows.clear();
                rows.reserve(std::size_t{ one.mSide } * one.mSide * 4);
                for (std::uint32_t texel = 0; texel < one.mSide * one.mSide; ++texel)
                    rows.insert(rows.end(), one.mColour.begin(), one.mColour.end());

                Testing::writeTexture(mRenderer, one.mSlot, GuiRegion{ 0, 0, one.mSide, one.mSide }, rows);
            }

            // The corners, because a run that overlapped its neighbour's is wrong at an edge before
            // it is wrong in the middle.
            for (const Written& one : written)
            {
                EXPECT_EQ(inTexture(one.mSlot, one.mSide, 0, 0), one.mColour) << "first texel of " << one.mSide;
                EXPECT_EQ(inTexture(one.mSlot, one.mSide, one.mSide - 1, one.mSide - 1), one.mColour)
                    << "last texel of " << one.mSide;
            }
        }

        /// A caller that produces its pixels writes them where the copy reads them, and they land.
        ///
        /// **What MyGUI's `lock` and `unlock` are answered with, and the copy they would cost.** A
        /// backend that lends a buffer of its own has to copy that buffer here afterwards, and a
        /// video frame then crosses main memory twice on its way to a device it could have been
        /// written into once.
        ///
        /// Every texel carries its own index, so a run handed out at the wrong offset is wrong at a
        /// row boundary rather than only in the middle of one.
        TEST_F(RtxGuiDrawTest, pixelsWrittenWhereTheDeviceReadsThemLandInTheTexture)
        {
            constexpr std::uint32_t side = 4;

            const GuiSlot texture = mRenderer.addGuiTexture(side, side);
            mHeld.push_back(texture);

            const std::span<std::uint8_t> into = mRenderer.lendGuiTexture(texture, GuiRegion{ 0, 0, side, side });
            ASSERT_EQ(into.size(), std::size_t{ side } * side * 4);

            for (std::uint32_t texel = 0; texel < side * side; ++texel)
            {
                into[texel * 4] = static_cast<std::uint8_t>(texel);
                into[texel * 4 + 1] = 0;
                into[texel * 4 + 2] = 0;
                into[texel * 4 + 3] = 255;
            }

            mRenderer.sendGuiTexture(texture);

            mRenderer.readGuiTexture(texture, mPixels);
            ASSERT_EQ(mPixels.size(), std::size_t{ side } * side * 4);

            for (std::uint32_t texel = 0; texel < side * side; ++texel)
            {
                const std::array<std::uint8_t, 4> found{ mPixels[texel * 4], mPixels[texel * 4 + 1],
                    mPixels[texel * 4 + 2], mPixels[texel * 4 + 3] };
                EXPECT_EQ(found, (std::array<std::uint8_t, 4>{ static_cast<std::uint8_t>(texel), 0, 0, 255 }))
                    << "texel " << texel;
            }
        }

        /// **Staging lent in one frame is never lent again while a copy out of it may not have
        /// run**, however many frames go by without the queue being waited on. A write into staging
        /// is a `memcpy` through a mapped pointer rather than a Vulkan command, so bytes handed out
        /// twice are invisible to synchronisation validation and show only as a texture holding a
        /// later frame's colour.
        ///
        /// Nine frames, each lending a texel of its own texture a colour of its own and drawing,
        /// and nothing read until the last: frame `f` writes `(20 f, 255 - 20 f, 7 f, 255)`, and
        /// each texture holds its own frame's colour at the end.
        TEST_F(RtxGuiDrawTest, stagingLentInOneFrameIsNotLentAgainBeforeItsCopyHasRun)
        {
            constexpr std::uint32_t frames = 9;
            const auto colourOf = [](std::uint32_t frame) {
                return std::array<std::uint8_t, 4>{ static_cast<std::uint8_t>(20 * frame),
                    static_cast<std::uint8_t>(255 - 20 * frame), static_cast<std::uint8_t>(7 * frame), 255 };
            };

            std::array<GuiSlot, frames> textures{};
            for (std::uint32_t frame = 0; frame < frames; ++frame)
            {
                textures[frame] = mRenderer.addGuiTexture(1, 1);
                mHeld.push_back(textures[frame]);

                const std::array<std::uint8_t, 4> colour = colourOf(frame);
                const std::span<std::uint8_t> into = mRenderer.lendGuiTexture(textures[frame], GuiRegion{ 0, 0, 1, 1 });
                std::copy(colour.begin(), colour.end(), into.begin());
                mRenderer.sendGuiTexture(textures[frame]);

                drawQuad(textures[frame], -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(255, 255, 255, 255));
            }

            for (std::uint32_t frame = 0; frame < frames; ++frame)
                EXPECT_EQ(inTexture(textures[frame], 1, 0, 0), colourOf(frame)) << "frame " << frame;
        }

        /// A texture given back while a write to it is still pending is let go without complaint.
        ///
        /// **The assertion is the validation sweep in `TearDown`.** Nothing has been submitted when
        /// the slot is dropped, so the image being destroyed is one a recorded command still names —
        /// which is a use after free unless what was recorded is submitted first.
        TEST_F(RtxGuiDrawTest, aTextureDroppedWithAWritePendingIsLetGoCleanly)
        {
            const GuiSlot texture = mRenderer.addGuiTexture(2, 2);

            std::array<std::uint8_t, 2 * 2 * 4> red{};
            for (std::size_t at = 0; at < red.size(); at += 4)
            {
                red[at] = 255;
                red[at + 3] = 255;
            }
            Testing::writeTexture(mRenderer, texture, GuiRegion{ 0, 0, 2, 2 }, red);

            mRenderer.dropGuiTexture(texture);

            const GuiSlot again = mRenderer.addGuiTexture(2, 2);
            EXPECT_EQ(again, texture) << "the freed slot, not a new one";
            mRenderer.dropGuiTexture(again);
        }

        /// **Two lends of one frame sit end to end in one run of staging**, and both copies land: a
        /// frame's writes ride one submit, so nothing reads the first region's bytes before the
        /// second is written, and the second is not the first rewound. Each region is 4 × 4 texels
        /// of four bytes, 64 bytes, which is a whole number of the staging's 16-byte alignment, so
        /// the second starts exactly 64 bytes after the first.
        ///
        /// **And a lend larger than a block of staging takes a block of its own**: 1536 × 1536
        /// texels are 9 437 184 bytes, past the 8 MiB block, and its far corner lands too.
        TEST_F(RtxGuiDrawTest, lendsOfOneFrameSitEndToEndAndALendPastABlockTakesItsOwn)
        {
            constexpr std::uint32_t side = 4;
            const GuiRegion whole{ 0, 0, side, side };
            const GuiSlot red = mRenderer.addGuiTexture(side, side);
            const GuiSlot green = mRenderer.addGuiTexture(side, side);
            mHeld.push_back(red);
            mHeld.push_back(green);

            std::array<std::uint8_t, side * side * 4> redRows{};
            std::array<std::uint8_t, side * side * 4> greenRows{};
            for (std::size_t at = 0; at < redRows.size(); at += 4)
            {
                redRows[at] = 255;
                redRows[at + 3] = 255;
                greenRows[at + 1] = 255;
                greenRows[at + 3] = 255;
            }

            const std::span<std::uint8_t> first = mRenderer.lendGuiTexture(red, whole);
            std::copy(redRows.begin(), redRows.end(), first.begin());
            mRenderer.sendGuiTexture(red);

            const std::span<std::uint8_t> second = mRenderer.lendGuiTexture(green, whole);
            EXPECT_EQ(second.data(), first.data() + redRows.size()) << "the second region is not after the first";
            std::copy(greenRows.begin(), greenRows.end(), second.begin());
            mRenderer.sendGuiTexture(green);

            EXPECT_EQ(inTexture(red, side, side - 1, side - 1), (std::array<std::uint8_t, 4>{ 255, 0, 0, 255 }));
            EXPECT_EQ(inTexture(green, side, side - 1, side - 1), (std::array<std::uint8_t, 4>{ 0, 255, 0, 255 }));

            constexpr std::uint32_t large = 1536;
            static_assert(std::size_t{ large } * large * 4 > std::size_t{ 8 } * 1024 * 1024);
            const GuiSlot wide = mRenderer.addGuiTexture(large, large);
            mHeld.push_back(wide);

            const std::span<std::uint8_t> big = mRenderer.lendGuiTexture(wide, GuiRegion{ 0, 0, large, large });
            ASSERT_EQ(big.size(), std::size_t{ large } * large * 4);
            std::fill(big.begin(), big.end(), std::uint8_t{ 0 });
            const std::array<std::uint8_t, 4> blue{ 0, 0, 255, 255 };
            std::copy(blue.begin(), blue.end(), big.end() - 4);
            mRenderer.sendGuiTexture(wide);

            EXPECT_EQ(inTexture(wide, large, large - 1, large - 1), blue);
            EXPECT_EQ(inTexture(wide, large, 0, 0), (std::array<std::uint8_t, 4>{ 0, 0, 0, 0 }));
        }

        /// The GUI over a frame that was actually traced, which is the first time the two halves of
        /// this renderer meet.
        ///
        /// **Nothing here asserts a radiance.** What the trace makes of a bare wall is the trace's
        /// business and is asserted at length elsewhere; what matters is that the GUI lands on top
        /// of it and leaves the rest of the picture exactly as the trace left it.
        TEST_F(RtxGuiDrawTest, theGuiLandsOverATracedFrameAndLeavesTheRestOfItAlone)
        {
            SceneDesc scene;
            Testing::addQuad(scene, Testing::wallAt(200.0f));

            mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});

            const Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(), osg::Vec3f(0.0f, 100.0f, 0.0f), 60.0f, sExtent, sExtent, 1000000.0f);

            mRenderer.renderFrame(camera, FrameOptions{});
            const std::array<std::uint8_t, 4> traced = at(sExtent - 2, 4);

            // The same camera and the same scene, so the same picture — and then the GUI over half
            // of it.
            mRenderer.renderFrame(camera, FrameOptions{});

            const GuiSlot texture = makeTexel({ 17, 34, 51, 255 });
            drawQuad(texture, -1.0f, 1.0f, 0.0f, -1.0f, Testing::packColour(255, 255, 255, 255));

            EXPECT_EQ(at(1, 4), (std::array<std::uint8_t, 4>{ 17, 34, 51, 255 })) << "where the GUI drew";
            EXPECT_EQ(at(sExtent - 2, 4), traced) << "where it did not";
        }

        /// A level sheet of `extent` about the origin at z = 0, facing up.
        /// @param fade how much of the sheet the placement shows, as the game fades an actor.
        SceneDesc makeSheet(float extent, float fade = 1.0f)
        {
            SceneDesc scene;
            scene.addInstance(
                MeshInstance{ .mMesh = Testing::addQuadMesh(scene, Testing::sheetAt(extent, 0.0f)), .mOpacity = fade });

            return scene;
        }

        /// Straight down at a sheet from a hundred units up, over a box two hundred across.
        Shaders::VisibilityConstants makeMapCamera(std::uint32_t extent)
        {
            Shaders::VisibilityConstants camera = makeOrthographicCameraFromView(
                osg::Matrixf::lookAt(osg::Vec3f(0.0f, 0.0f, 100.0f), osg::Vec3f(), osg::Vec3f(0.0f, 1.0f, 0.0f)),
                200.0f, 200.0f, extent, extent, 1.0f, 10000.0f)
                                                      .value();

            // Travelling straight down onto a sheet that faces up, so it is lit square on and the
            // picture is something rather than a coverage mask with nothing in it.
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(1.0f, 1.0f, 1.0f));

            return camera;
        }

        /// The sheet `makeMapCamera` lights, as the display pass writes it.
        ///
        /// A default albedo of a half, Lambertian, square to a sun of one: `0.5 * 1.0 / pi = 0.159155`
        /// linear, at the exposure of one a picture is held at, through the curve `displayedGrey`
        /// follows — 98 of 255.
        std::array<std::uint8_t, 4> sheetLit()
        {
            const std::uint8_t grey = Testing::displayedGrey(0.5f * Shaders::INV_PI);
            return { grey, grey, grey, 255 };
        }

        /// A picture traced into the table the GUI draws from, and where it stops.
        ///
        /// **The shape is the assertion and it is counted by hand.** The sheet is fifty units across
        /// inside a box two hundred across, so it covers a quarter of each axis: pixel `p` of sixteen
        /// samples the world at `100 * ((p + 0.5) / 8 - 1)`, which is inside twenty-five for `p` in
        /// 6..9. Four columns, four rows, and nothing on the boundary for rounding to argue over.
        ///
        /// What the two legs differ in is the one field that says so: with a sky behind it every
        /// pixel is opaque, and without one the picture says where it stops.
        TEST_F(RtxGuiDrawTest, aTracedPictureFillsAGuiTextureAndSaysWhereItStops)
        {
            constexpr std::uint32_t extent = 16;

            mRenderer.setScene(Rtx::SceneSlot::world(), makeSheet(25.0f), {});

            const GuiSlot texture = mRenderer.addGuiTexture(extent, extent);
            mHeld.push_back(texture);

            Shaders::VisibilityConstants camera = makeMapCamera(extent);
            camera.mTransparentBackground = 1;

            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{});

            for (std::uint32_t p : { 6u, 9u })
            {
                EXPECT_EQ(inTexture(texture, extent, p, 8)[3], 255) << "covered, column " << p;
                EXPECT_EQ(inTexture(texture, extent, 8, p)[3], 255) << "covered, row " << p;
            }

            for (std::uint32_t p : { 5u, 10u })
            {
                EXPECT_EQ(inTexture(texture, extent, p, 8), (std::array<std::uint8_t, 4>{ 0, 0, 0, 0 }))
                    << "past the sheet, column " << p;
                EXPECT_EQ(inTexture(texture, extent, 8, p), (std::array<std::uint8_t, 4>{ 0, 0, 0, 0 }))
                    << "past the sheet, row " << p;
            }

            // **Lit, and to the byte**, so the whole chain ran rather than only the coverage the
            // alpha above would have had either way.
            EXPECT_EQ(inTexture(texture, extent, 8, 8), sheetLit()) << "the sheet, lit";

            // The same picture with a sky behind it, which is what a frame filling a window has:
            // every pixel opaque, the corner included.
            //
            // **Twice, with nothing between.** A picture that fills its texture never clears it, so
            // this is the only path where the copy is the first thing to write it — and the second
            // of the two starts from a texture the first left where it found it rather than from
            // one nothing has touched. What it is really asking is whether the two are ordered at
            // all, which is a question only the synchronization layers answer.
            camera.mTransparentBackground = 0;
            for (int again = 0; again < 2; ++again)
                mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{});

            EXPECT_EQ(inTexture(texture, extent, 5, 8), (std::array<std::uint8_t, 4>{ 0, 0, 0, 255 }))
                << "past the sheet, and opaque";

            // **And a see-through sheet over nothing is there as much as it is anywhere,
            // premultiplied.** Faded to a quarter and lit four times as brightly, its light over
            // nothing is the whole sheet's at one — `sheetLit`'s grey — and its alpha is the quarter:
            // the backdrop it leaves is `0.75 * 255` = 191.25, held as 191, so `1 - 191 / 255` is
            // 64 of 255 exactly. Taken straight, its colour would be four times the sheet's; covered
            // by the hit alone, it would not be drawn at all.
            mRenderer.setScene(Rtx::SceneSlot::world(), makeSheet(25.0f, 0.25f), {});
            camera.mTransparentBackground = 1;
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));
            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{});

            const std::uint8_t grey = sheetLit()[0];
            EXPECT_EQ(inTexture(texture, extent, 8, 8), (std::array<std::uint8_t, 4>{ grey, grey, grey, 64 }))
                << "a quarter of the sheet, over nothing";
            EXPECT_EQ(inTexture(texture, extent, 5, 8), (std::array<std::uint8_t, 4>{ 0, 0, 0, 0 }))
                << "and nothing past it";
        }

        /// Two pictures of a subject scene, placed and traced twice with nothing waited between,
        /// each show the placement they were traced after.
        ///
        /// **The doll's slot discipline.** A race slider drag places and traces the same scene every
        /// frame; the second placement has to go into the copy the first trace is not reading, or
        /// the first picture shows the second placement. Read only after both are recorded.
        TEST_F(RtxGuiDrawTest, twoPicturesOfOneSubjectSceneEachShowTheirOwnPlacement)
        {
            constexpr std::uint32_t extent = 16;

            mRenderer.setScene(Rtx::SceneSlot::world(), makeSheet(100.0f), {});

            SceneDesc doll = makeSheet(25.0f);
            const SceneSlot slot = mRenderer.addViewScene();
            mRenderer.setScene(slot, doll, {});

            const GuiSlot first = mRenderer.addGuiTexture(extent, extent);
            const GuiSlot second = mRenderer.addGuiTexture(extent, extent);
            mHeld.push_back(first);
            mHeld.push_back(second);

            Shaders::VisibilityConstants camera = makeMapCamera(extent);
            camera.mTransparentBackground = 1;
            const GuiTraceOptions options{ .mScene = slot };

            mRenderer.traceGuiTexture(first, camera, options);

            ASSERT_TRUE(doll.placements().move(0, osg::Matrixf::translate(1000.0f, 0.0f, 0.0f)));
            mRenderer.placeScene(slot, doll);
            mRenderer.traceGuiTexture(second, camera, options);

            EXPECT_EQ(inTexture(first, extent, 8, 8)[3], 255) << "the sheet where it stood when the first was traced";
            EXPECT_EQ(inTexture(second, extent, 8, 8)[3], 0) << "and gone by the second";

            mRenderer.dropViewScene(slot);
        }

        /// The copy a trace leaves for the host is the texture, byte for byte, and only where one was
        /// asked for.
        TEST_F(RtxGuiDrawTest, aTraceLeavesTheCopyItWasAskedForAndNoOther)
        {
            constexpr std::uint32_t extent = 16;

            mRenderer.setScene(Rtx::SceneSlot::world(), makeSheet(25.0f), {});

            const GuiSlot texture = mRenderer.addGuiTexture(extent, extent);
            mHeld.push_back(texture);

            std::vector<std::uint8_t> copy(std::size_t{ extent } * extent * 4, 1);

            const Shaders::VisibilityConstants camera = makeMapCamera(extent);
            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{});
            mRenderer.finishGuiTraces();
            EXPECT_FALSE(mRenderer.takeGuiCopy(texture, copy)) << "nothing asked for a copy";

            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{ .mReadBack = true });
            mRenderer.finishGuiTraces();
            ASSERT_TRUE(mRenderer.takeGuiCopy(texture, copy));

            mRenderer.readGuiTexture(texture, mPixels);
            EXPECT_EQ(copy, mPixels);
            EXPECT_EQ(Testing::rgbaAt(copy, extent, 8, 8), sheetLit());
        }

        /// A camera's mask is what its rays meet: a class the mask leaves out is not in the picture.
        ///
        /// **Coverage and not colour**, because two lit sheets of the default albedo are the same
        /// grey. A static sheet stands to one side and an actor's to the other, over a transparent
        /// background: `makeMapCamera` samples pixel `p` at `100 * ((p + 0.5) / 8 - 1)`, so a sheet
        /// of fifty about x = -50 covers columns 2..5 and one about x = 50 covers 10..13. A camera
        /// asking for every class covers both; one asking for the statics alone leaves the actor's
        /// side at the clear colour, which is what a map tile does with the people on it.
        TEST_F(RtxGuiDrawTest, aCameraLeavesOutTheClassesItsMaskDoesNotName)
        {
            constexpr std::uint32_t extent = 16;

            SceneDesc scene;
            const Index sheet = Testing::addQuadMesh(scene, Testing::sheetAt(25.0f, 0.0f));
            scene.addInstance(
                MeshInstance{ .mTransform = osg::Matrixf::translate(-50.0f, 0.0f, 0.0f), .mMesh = sheet });
            scene.addInstance(MeshInstance{ .mTransform = osg::Matrixf::translate(50.0f, 0.0f, 0.0f),
                .mMesh = sheet,
                .mClass = InstanceClass::Actor });
            mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});

            const GuiSlot texture = mRenderer.addGuiTexture(extent, extent);
            mHeld.push_back(texture);

            Shaders::VisibilityConstants camera = makeMapCamera(extent);
            camera.mTransparentBackground = 1;

            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{});
            EXPECT_EQ(inTexture(texture, extent, 3, 8)[3], 255) << "the static, under every class";
            EXPECT_EQ(inTexture(texture, extent, 12, 8)[3], 255) << "the actor, under every class";

            camera.mRayMask = Shaders::MASK_STATIC;
            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{});
            EXPECT_EQ(inTexture(texture, extent, 3, 8)[3], 255) << "the static, under the statics alone";
            EXPECT_EQ(inTexture(texture, extent, 12, 8)[3], 0) << "the actor, left out";
        }

        /// A camera without `MASK_PARTICLE` bins no sprites and draws none, and reads an empty list
        /// rather than whatever the slot's last bin left there. A camera with it bins the scene it
        /// looks at, a subject's as much as the world's.
        ///
        /// A white puff hangs over the middle of the sheet, square to the sun. With the bit the
        /// centre pixel is the puff over the sheet and not the sheet's own grey; without it the
        /// centre is the lit sheet exactly as `sheetLit` counts it. The frame between the two is what
        /// leaves a bin of the puff in the slot's list for the second picture to ignore.
        TEST_F(RtxGuiDrawTest, aCameraWithoutTheParticleBitDrawsNoSprites)
        {
            constexpr std::uint32_t extent = 16;
            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 1> puff{ Testing::describeTexel(white) };

            SceneDesc scene = makeSheet(25.0f);
            const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
            const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 50.0f),
                .mRadius = 30.0f,
                .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                .mAlpha = 1.0f } };
            scene.addEmitter(sprites, cut, false);
            mRenderer.setScene(Rtx::SceneSlot::world(), scene, puff);

            const GuiSlot texture = mRenderer.addGuiTexture(extent, extent);
            mHeld.push_back(texture);

            const Shaders::VisibilityConstants camera = makeMapCamera(extent);
            const std::array<std::uint8_t, 4> lit = sheetLit();

            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{});
            EXPECT_NE(inTexture(texture, extent, 8, 8), lit) << "the puff over the sheet";

            Shaders::VisibilityConstants frame = camera;
            frame.mCamera.mWidth = sExtent;
            frame.mCamera.mHeight = sExtent;
            mRenderer.renderFrame(frame, FrameOptions{});

            Shaders::VisibilityConstants chart = camera;
            chart.mRayMask &= ~Shaders::MASK_PARTICLE;
            mRenderer.traceGuiTexture(texture, chart, GuiTraceOptions{});
            EXPECT_EQ(inTexture(texture, extent, 8, 8), lit) << "the sheet alone";

            // The same scene as a subject, binned into its own tables and not the frame's.
            const SceneSlot subject = mRenderer.addViewScene();
            mRenderer.setScene(subject, scene, puff);
            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{ .mScene = subject });
            EXPECT_NE(inTexture(texture, extent, 8, 8), lit) << "the puff over the subject's sheet";
            mRenderer.dropViewScene(subject);
        }

        /// A puff laid over nothing covers a picture as much as it covers anything, and a flame laid
        /// over nothing is light that covers nothing at all.
        ///
        /// **Held against the same picture over black**, because what a puff lets through at a pixel
        /// is the march's to say and not arithmetic a test can do by hand: premultiplied, a layer's
        /// colour over nothing is its colour over black to the byte, and what it covers is what the
        /// backdrop no longer shows. The white puff of `aCameraWithoutTheParticleBitDrawsNoSprites`
        /// grown to sixty units across a sheet twenty-five across, so column 4 of row 8, whose
        /// middle stands 44.2 units off the puff's, is the puff over nothing. The flame is the same
        /// ball, adding.
        TEST_F(RtxGuiDrawTest, aPuffOverNothingIsThereAsMuchAsItIsAnywhere)
        {
            constexpr std::uint32_t extent = 16;
            constexpr std::array<std::uint8_t, 4> white{ 255, 255, 255, 255 };
            const std::array<TextureData, 1> puff{ Testing::describeTexel(white) };

            const GuiSlot texture = mRenderer.addGuiTexture(extent, extent);
            mHeld.push_back(texture);

            struct Laid
            {
                std::array<std::uint8_t, 4> mOverBlack;
                std::array<std::uint8_t, 4> mOverNothing;
            };

            const auto laidWith = [&](bool additive) {
                SceneDesc scene = makeSheet(25.0f);
                const Index cut = scene.textures().add(VFS::Path::NormalizedView("sprite.dds"));
                const std::array<Sprite, 1> sprites{ Sprite{ .mPosition = osg::Vec3f(0.0f, 0.0f, 50.0f),
                    .mRadius = 60.0f,
                    .mColour = osg::Vec3f(1.0f, 1.0f, 1.0f),
                    .mAlpha = 1.0f } };
                scene.addEmitter(sprites, cut, additive);
                mRenderer.setScene(Rtx::SceneSlot::world(), scene, puff);

                Shaders::VisibilityConstants camera = makeMapCamera(extent);
                Laid laid{};
                mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{});
                laid.mOverBlack = inTexture(texture, extent, 4, 8);

                camera.mTransparentBackground = 1;
                mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{});
                laid.mOverNothing = inTexture(texture, extent, 4, 8);

                return laid;
            };

            const Laid puffed = laidWith(false);
            ASSERT_EQ(puffed.mOverBlack[3], 255) << "a picture over black is opaque";
            ASSERT_GT(puffed.mOverBlack[0], 0) << "the puff did not reach the pixel";
            EXPECT_EQ(puffed.mOverNothing[0], puffed.mOverBlack[0]) << "the puff's colour, premultiplied";
            EXPECT_GT(puffed.mOverNothing[3], 0) << "the puff covered nothing of the picture";
            EXPECT_LT(puffed.mOverNothing[3], 255) << "and a ball's rim is not the whole of it";

            const Laid flame = laidWith(true);
            ASSERT_GT(flame.mOverBlack[0], 0) << "the flame did not reach the pixel";
            EXPECT_EQ(flame.mOverNothing,
                (std::array<std::uint8_t, 4>{ flame.mOverBlack[0], flame.mOverBlack[1], flame.mOverBlack[2], 0 }))
                << "a flame over nothing is its light and no coverage";
        }

        /// A picture smaller than the texture behind it, which is the inventory doll: its window
        /// resizes and the texture does not.
        ///
        /// **The clear colour is exact.** It is written into an eight-bit unorm image, so one is
        /// 255 and not 254 — there is no encoding between the float and the byte.
        TEST_F(RtxGuiDrawTest, aPictureShorterThanItsTextureLeavesTheRestAtTheClearColour)
        {
            constexpr std::uint32_t extent = 16;
            constexpr std::uint32_t filled = 8;

            mRenderer.setScene(Rtx::SceneSlot::world(), makeSheet(25.0f), {});

            const GuiSlot texture = mRenderer.addGuiTexture(extent, extent);
            mHeld.push_back(texture);

            Shaders::VisibilityConstants camera = makeMapCamera(filled);
            camera.mTransparentBackground = 1;

            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{ .mClear = { 1.0f, 0.0f, 0.0f, 1.0f } });

            // The sheet now covers a quarter of eight pixels — `p` in 3..4 — so the middle of the
            // filled corner is on it and the corner past `filled` was never traced at all.
            EXPECT_EQ(inTexture(texture, extent, 4, 4)[3], 255) << "inside the picture";
            EXPECT_EQ(inTexture(texture, extent, filled, filled), (std::array<std::uint8_t, 4>{ 255, 0, 0, 255 }))
                << "just past it";
            EXPECT_EQ(
                inTexture(texture, extent, extent - 1, extent - 1), (std::array<std::uint8_t, 4>{ 255, 0, 0, 255 }))
                << "the far corner";
        }

        /// A picture of something that is not in the world at all: the inventory doll.
        ///
        /// **The two scenes are told apart by their shape and the count is exact.** The world holds
        /// a sheet that fills the box; the view scene holds one covering a quarter of each axis. Of
        /// sixteen pixels across, the first gives sixteen covered and the second four — `p` in 6..9,
        /// as above. Nothing about a doll may reach the world's geometry, and nothing about the
        /// frame may reach the doll's.
        TEST_F(RtxGuiDrawTest, aPictureCanBeOfASceneTheWorldDoesNotHold)
        {
            constexpr std::uint32_t extent = 16;

            // Two hundred across is the whole box, so the world's sheet covers every pixel.
            mRenderer.setScene(Rtx::SceneSlot::world(), makeSheet(100.0f), {});

            const SceneSlot doll = mRenderer.addViewScene();
            mRenderer.setScene(doll, makeSheet(25.0f), {});

            const GuiSlot texture = mRenderer.addGuiTexture(extent, extent);
            mHeld.push_back(texture);

            Shaders::VisibilityConstants camera = makeMapCamera(extent);
            camera.mTransparentBackground = 1;

            const auto covered = [&](SceneSlot scene) {
                mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{ .mScene = scene });

                std::uint32_t across = 0;
                for (std::uint32_t x = 0; x < extent; ++x)
                    if (inTexture(texture, extent, x, 8)[3] != 0)
                        ++across;

                return across;
            };

            EXPECT_EQ(covered(SceneSlot::world()), extent) << "the world fills the box";
            EXPECT_EQ(covered(doll), 4u) << "and the doll is a quarter of it";

            // The world is still the world afterwards: building one scene did not replace the other.
            EXPECT_EQ(covered(SceneSlot::world()), extent) << "the world, still there";

            mRenderer.dropViewScene(doll);
        }

        /// A placement into a view scene reaches the picture traced from it afterwards.
        ///
        /// **The pair a doll pays, and nothing else here drives it.** `placeScene` submits and waits
        /// and the trace submits and waits again, so what makes the second read what the first wrote
        /// is the order of two submits. The world's placement rides a frame instead, and
        /// `OffscreenTrace`'s own tests hand their calls to a renderer that counts them.
        ///
        /// **And a placement names one scene.** Moving the doll must leave the world's sheet where
        /// it is, which is the other half of the assertion.
        TEST_F(RtxGuiDrawTest, aPlacementIntoAViewSceneReachesThePictureAndLeavesTheWorldAlone)
        {
            constexpr std::uint32_t extent = 16;

            const SceneDesc world = makeSheet(100.0f);
            SceneDesc doll = makeSheet(25.0f);

            mRenderer.setScene(Rtx::SceneSlot::world(), world, {});

            const SceneSlot slot = mRenderer.addViewScene();
            mRenderer.setScene(slot, doll, {});

            const GuiSlot texture = mRenderer.addGuiTexture(extent, extent);
            mHeld.push_back(texture);

            Shaders::VisibilityConstants camera = makeMapCamera(extent);
            camera.mTransparentBackground = 1;

            const auto covered = [&](SceneSlot scene) {
                mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{ .mScene = scene });

                std::uint32_t across = 0;
                for (std::uint32_t x = 0; x < extent; ++x)
                    if (inTexture(texture, extent, x, 8)[3] != 0)
                        ++across;

                return across;
            };

            EXPECT_EQ(covered(slot), 4u) << "the doll before anything moved";

            // Out of the camera's box of two hundred altogether, so what the placement did shows as
            // the picture emptying rather than as a sheet a pixel narrower.
            ASSERT_TRUE(doll.placements().move(0, osg::Matrixf::translate(1000.0f, 0.0f, 0.0f)));
            mRenderer.placeScene(slot, doll);

            EXPECT_EQ(covered(slot), 0u) << "the placement did not reach the trace";

            // **Two placements before one trace**, which is what a drag does. The picture is the
            // second, so a scheme that carried only the first would show the sheet back in the box.
            ASSERT_TRUE(doll.placements().move(0, osg::Matrixf::identity()));
            mRenderer.placeScene(slot, doll);
            ASSERT_TRUE(doll.placements().move(0, osg::Matrixf::translate(1000.0f, 0.0f, 0.0f)));
            mRenderer.placeScene(slot, doll);

            EXPECT_EQ(covered(slot), 0u) << "the trace showed the first of two placements";

            ASSERT_TRUE(doll.placements().move(0, osg::Matrixf::identity()));
            mRenderer.placeScene(slot, doll);

            EXPECT_EQ(covered(slot), 4u) << "a placement brought it back";
            EXPECT_EQ(covered(SceneSlot::world()), extent) << "and none of it took the world with it";

            mRenderer.dropViewScene(slot);
        }

        /// The picture the trace made is the picture the GUI draws with, which is the whole point of
        /// it going into a slot rather than coming back to main memory.
        TEST_F(RtxGuiDrawTest, theGuiDrawsWithAPictureTheTraceMade)
        {
            constexpr std::uint32_t extent = 16;

            mRenderer.setScene(Rtx::SceneSlot::world(), makeSheet(25.0f), {});

            const GuiSlot texture = mRenderer.addGuiTexture(extent, extent);
            mHeld.push_back(texture);

            Shaders::VisibilityConstants camera = makeMapCamera(extent);
            camera.mTransparentBackground = 1;
            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{});

            // Blue underneath, then the traced picture over the whole frame. The middle samples the
            // sheet, which is opaque and covers the blue; the corner samples where the trace stopped,
            // which is transparent and leaves it.
            const GuiSlot white = makeTexel({ 255, 255, 255, 255 });
            drawQuad(white, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(0, 0, 255, 255));
            drawQuad(texture, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(255, 255, 255, 255));

            EXPECT_NE(at(4, 4), (std::array<std::uint8_t, 4>{ 0, 0, 255, 255 })) << "where the picture covers";
            EXPECT_EQ(at(0, 0), (std::array<std::uint8_t, 4>{ 0, 0, 255, 255 })) << "where it does not";

            // **Laid down premultiplied, and faded as a window fades.** The quarter of a sheet
            // `aTracedPictureFillsAGuiTextureAndSaysWhereItStops` holds to `sheetLit`'s grey `g` at
            // an alpha of 64, over grey, through a vertex alpha of `f = 128 / 255`: the colour is taken
            // as it is and weighed by `f`, and the grey is left `1 - f * 64 / 255` of itself. At the
            // 98 `g` comes to that is `98 f + 128 (1 - f * 64 / 255)` = 49.19 + 111.87 = 161.07, or
            // 161; blended straight it would be `98 * (f * 64 / 255) + 128 (1 - f * 64 / 255)` =
            // 12.35 + 111.87 = 124.2, or 124.
            mRenderer.setScene(Rtx::SceneSlot::world(), makeSheet(25.0f, 0.25f), {});
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(4.0f, 4.0f, 4.0f));
            mRenderer.traceGuiTexture(texture, camera, GuiTraceOptions{});

            const std::uint8_t g = sheetLit()[0];
            ASSERT_EQ(g, 98);
            ASSERT_EQ(inTexture(texture, extent, 8, 8), (std::array<std::uint8_t, 4>{ g, g, g, 64 }));

            const GuiSlot grey = makeTexel({ 128, 128, 128, 255 });
            drawQuad(grey, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(255, 255, 255, 255));
            drawQuad(texture, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(255, 255, 255, 128));

            const float fade = 128.0f / 255.0f;
            const float covered = fade * 64.0f / 255.0f;
            const auto laid
                = static_cast<std::uint8_t>(std::lround(static_cast<float>(g) * fade + 128.0f * (1.0f - covered)));
            ASSERT_EQ(laid, 161);
            EXPECT_EQ(at(sExtent / 2, sExtent / 2), (std::array<std::uint8_t, 4>{ laid, laid, laid, 255 }))
                << "the picture over grey, through a window half faded";
        }

        /// A picture inside the interface leaves the frame's own exposure where it found it.
        ///
        /// **The eye carries between frames and a picture has none.** A picture is mapped at one and
        /// is traced between two world frames, and one written into the frame's buffer would be read
        /// back by the next frame as the brightness it had adapted to —
        /// `ExposurePass::getPictureExposure` says what every arriving local-map tile would cost.
        ///
        /// **The claim is exact.** Both legs draw one camera over one scene from one reset, so the
        /// frame after the picture is the frame after no picture, byte for byte. The third leg is
        /// what says the comparison could have failed at all: the same frame at an exposure of one
        /// is a different picture, and one is what a picture writes.
        TEST_F(RtxGuiDrawTest, aPictureInsideTheInterfaceLeavesTheFramesExposureAlone)
        {
            constexpr std::uint32_t extent = 16;

            mRenderer.setScene(Rtx::SceneSlot::world(), makeSheet(25.0f), {});

            const GuiSlot texture = mRenderer.addGuiTexture(extent, extent);
            mHeld.push_back(texture);

            Shaders::VisibilityConstants picture = makeMapCamera(extent);
            picture.mTransparentBackground = 1;

            // Level, two hundred units over a sheet fifty across, so the frame is sky alone and the
            // sky is the one thing the two cameras below differ in. Far enough apart that the
            // exposure the first leaves behind is nowhere near the one a picture writes.
            Shaders::VisibilityConstants bright = Testing::makeCamera(
                osg::Vec3f(0.0f, 0.0f, 200.0f), osg::Vec3f(0.0f, 1000.0f, 200.0f), 60.0f, sExtent, sExtent, 100000.0f);
            bright.mSkyHorizon = osg::Vec3f(0.8f, 0.8f, 0.8f);
            bright.mSkyZenith = bright.mSkyHorizon;
            bright.mAmbientFromSky = 1.0f;

            Shaders::VisibilityConstants dim = bright;
            dim.mSkyHorizon = bright.mSkyHorizon / 32.0f;
            dim.mSkyZenith = dim.mSkyHorizon;

            // **A step, and the same one on both legs**: a measured exposure adapts by how long
            // since the last frame, `FrameOptions::mSinceLast`, and a frame that states nought
            // adapts nothing at all.
            constexpr float sStep = 1.0f / 60.0f;

            const auto frame = [&](const Shaders::VisibilityConstants& camera, std::optional<float> exposure) {
                mRenderer.renderFrame(
                    camera, FrameOptions{ .mSinceLast = sStep, .mExposure = ExposureRule{ .mFixed = exposure } });
                mRenderer.readPixels(mPixels);
                return mPixels;
            };

            // One reset, one bright frame the eye takes outright, then one dim frame it has barely
            // moved for — so what the dim frame looks like is what the bright frame's exposure made
            // of it, which is exactly what a picture between the two must not change.
            const auto dimFrameAfterBright = [&](bool withPicture) {
                mRenderer.resetHistory();
                frame(bright, std::nullopt);
                if (withPicture)
                    mRenderer.traceGuiTexture(texture, picture, GuiTraceOptions{});

                return frame(dim, std::nullopt);
            };

            const std::vector<std::uint8_t> carried = dimFrameAfterBright(false);
            const std::vector<std::uint8_t> afterPicture = dimFrameAfterBright(true);

            mRenderer.resetHistory();
            frame(bright, std::nullopt);
            const std::vector<std::uint8_t> atOne = frame(dim, 1.0f);

            ASSERT_NE(carried, atOne) << "the carried exposure and one draw the same picture here";
            EXPECT_EQ(afterPicture, carried) << "the picture took the frame's exposure with it";
        }

        /// Nothing to draw is not an error and does not touch the frame.
        TEST_F(RtxGuiDrawTest, anEmptyGuiLeavesTheFrameAlone)
        {
            const GuiSlot white = makeTexel({ 255, 255, 255, 255 });
            drawQuad(white, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(17, 34, 51, 255));

            mRenderer.drawGui({}, {});

            EXPECT_EQ(at(4, 4), (std::array<std::uint8_t, 4>{ 17, 34, 51, 255 }));
        }

        /// A texture given back outlives the interface that was drawn with it.
        ///
        /// **A window closes on the frame after the one it was last drawn on**, and that draw is
        /// still on the queue: the interface is submitted without being waited for, and its fence is
        /// read two frames later. So this walks the path a closing window takes with two draws
        /// still in flight, and the layers are what say whether anything was destroyed under one —
        /// the fixture fails the test on any complaint they make.
        TEST_F(RtxGuiDrawTest, aTextureGivenBackOutlivesTheDrawItWasUsedIn)
        {
            // Given back in the middle of the test rather than at the end of it, so it is not one
            // of the fixture's to hold.
            const GuiSlot closing = mRenderer.addGuiTexture(1, 1);
            Testing::writeTexture(mRenderer, closing, GuiRegion{ 0, 0, 1, 1 }, sWhite);

            // Two draws, neither waited for, which is both slots of the ring in flight at once.
            drawQuad(closing, -1.0f, 1.0f, 0.0f, 0.0f, Testing::packColour(255, 0, 0, 255));
            drawQuad(closing, 0.0f, 0.0f, 1.0f, -1.0f, Testing::packColour(0, 255, 0, 255));

            // The window closes, and the next one opens: making a texture and drawing with it must not
            // submit, wait for its own batch alone, and destroy the one above.
            mRenderer.dropGuiTexture(closing);

            const GuiSlot opening = makeTexel(sWhite);
            drawQuad(opening, -1.0f, 1.0f, 1.0f, -1.0f, Testing::packColour(0, 0, 255, 255));

            EXPECT_EQ(at(4, 4), (std::array<std::uint8_t, 4>{ 0, 0, 255, 255 }));
        }

        struct RtxGuiTexturesTest : Testing::DeviceTest
        {
        };

        /// **An interface texture's slot still out when the table is taken apart is named there**,
        /// for the reason `RtxSceneSlotsTest` gives. A slot taken, given back and handed over
        /// leaves nothing out; one taken and kept dies with the message.
        TEST_F(RtxGuiTexturesTest, aTableTakenApartWithASlotOutDies)
        {
            {
                GuiTextures textures(getDevice());
                textures.drop(textures.add(2, 2));
                textures.finish();
            }

            Testing::expectAssertDies(
                [&] {
                    GuiTextures textures(getDevice());
                    textures.add(2, 2);
                },
                "a renderer taken apart with an interface texture's slot still out");
        }
    }
}
