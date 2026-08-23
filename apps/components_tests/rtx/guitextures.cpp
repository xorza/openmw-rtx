#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/camera.hpp>
#include <components/rtx/instancerecord.hpp>
#include <components/rtx/renderer.hpp>
#include <components/rtx/scenedesc.hpp>

#include "harness.hpp"

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sExtent = 8;

        constexpr std::uint32_t packColour(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha)
        {
            return static_cast<std::uint32_t>(red) | (static_cast<std::uint32_t>(green) << 8)
                | (static_cast<std::uint32_t>(blue) << 16) | (static_cast<std::uint32_t>(alpha) << 24);
        }

        /// Two triangles of a rectangle in clip space, given MyGUI's orientation: `top` is the
        /// coordinate nearer +1.
        std::array<GuiVertex, 6> makeQuad(float left, float top, float right, float bottom, std::uint32_t colour)
        {
            const GuiVertex topLeft{ left, top, 0.0f, colour, 0.0f, 0.0f };
            const GuiVertex topRight{ right, top, 0.0f, colour, 1.0f, 0.0f };
            const GuiVertex bottomLeft{ left, bottom, 0.0f, colour, 0.0f, 1.0f };
            const GuiVertex bottomRight{ right, bottom, 0.0f, colour, 1.0f, 1.0f };

            return { topLeft, bottomLeft, bottomRight, topLeft, bottomRight, topRight };
        }

        /// The GUI as the renderer offers it: a table of textures and one call that draws with them.
        ///
        /// **No scene, and that is the point.** A main menu and a loading screen are drawn over a
        /// frame nothing traced, so everything here has to work before `setScene` has been called
        /// even once.
        class RtxGuiDrawTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                std::string reason;
                mRenderer = Testing::getRenderer(reason);
                if (mRenderer == nullptr)
                    GTEST_SKIP() << reason;

                mRenderer->resize(sExtent, sExtent);

                std::vector<std::string> stale;
                mRenderer->takeValidationErrors(stale);
            }

            void TearDown() override
            {
                if (mRenderer == nullptr)
                    return;

                for (std::uint32_t texture : mHeld)
                    mRenderer->dropGuiTexture(texture);
                mHeld.clear();

                std::vector<std::string> errors;
                mRenderer->takeValidationErrors(errors);
                for (const std::string& error : errors)
                    ADD_FAILURE() << "validation error: " << error;
            }

            /// A one-texel texture of this colour, dropped when the test ends.
            std::uint32_t makeTexel(std::array<std::uint8_t, 4> colour)
            {
                const std::uint32_t texture = mRenderer->addGuiTexture(1, 1);
                mHeld.push_back(texture);
                mRenderer->writeGuiTexture(texture, colour);
                return texture;
            }

            void drawQuad(std::uint32_t texture, float left, float top, float right, float bottom, std::uint32_t colour)
            {
                const std::array<GuiVertex, 6> quad = makeQuad(left, top, right, bottom, colour);
                const std::array<GuiBatch, 1> batches{ GuiBatch{ texture, 0, quad.size() } };
                mRenderer->drawGui(quad, batches);
            }

            /// The four bytes at a pixel of the presented frame, row zero at the top.
            std::array<std::uint8_t, 4> at(std::uint32_t x, std::uint32_t y)
            {
                mRenderer->readPixels(mPixels);
                EXPECT_EQ(mPixels.size(), std::size_t{ sExtent } * sExtent * 4);

                const std::size_t offset = (static_cast<std::size_t>(y) * sExtent + x) * 4;
                return { mPixels[offset], mPixels[offset + 1], mPixels[offset + 2], mPixels[offset + 3] };
            }

            Renderer* mRenderer = nullptr;
            std::vector<std::uint32_t> mHeld;
            std::vector<std::uint8_t> mPixels;
        };

        /// What a batch shows is its texture times its vertex colour, and nothing else.
        TEST_F(RtxGuiDrawTest, aBatchShowsItsTextureTimesItsVertexColour)
        {
            // Halves that divide exactly, so the multiply has nothing to round: 200 × 128/255 is not
            // an integer, but 200 × 1 and 100 × 1 are.
            const std::uint32_t texture = makeTexel({ 200, 100, 50, 255 });

            drawQuad(texture, -1.0f, 1.0f, 1.0f, -1.0f, packColour(255, 255, 255, 255));

            EXPECT_EQ(at(4, 4), (std::array<std::uint8_t, 4>{ 200, 100, 50, 255 }));
        }

        /// A second call draws over what the first left, rather than over whatever the frame held.
        ///
        /// **This is what makes a GUI out of one call per frame.** MyGUI produces its batches in
        /// layer order and the renderer draws them in that order; a pass that discarded the target
        /// each time would show only the last thing drawn.
        TEST_F(RtxGuiDrawTest, aSecondDrawLandsOverTheFirst)
        {
            const std::uint32_t white = makeTexel({ 255, 255, 255, 255 });

            drawQuad(white, -1.0f, 1.0f, 1.0f, -1.0f, packColour(0, 0, 255, 255));
            drawQuad(white, -1.0f, 1.0f, 0.0f, -1.0f, packColour(255, 0, 0, 128));

            // Exact: an alpha of 128/255 contributes 255 × 128/255 = 128 and leaves
            // 255 × 127/255 = 127 of what was there.
            EXPECT_EQ(at(1, 4), (std::array<std::uint8_t, 4>{ 128, 0, 127, 255 })) << "blended over";
            EXPECT_EQ(at(sExtent - 2, 4), (std::array<std::uint8_t, 4>{ 0, 0, 255, 255 })) << "left alone";
        }

        /// Writing a texture again changes what is drawn with it, which is the whole of what a video
        /// frame and a fog of war need.
        TEST_F(RtxGuiDrawTest, rewritingATextureChangesWhatIsDrawn)
        {
            const std::uint32_t texture = makeTexel({ 255, 0, 0, 255 });

            drawQuad(texture, -1.0f, 1.0f, 1.0f, -1.0f, packColour(255, 255, 255, 255));
            EXPECT_EQ(at(4, 4), (std::array<std::uint8_t, 4>{ 255, 0, 0, 255 })) << "as written";

            constexpr std::array<std::uint8_t, 4> sGreen{ 0, 255, 0, 255 };
            mRenderer->writeGuiTexture(texture, sGreen);

            drawQuad(texture, -1.0f, 1.0f, 1.0f, -1.0f, packColour(255, 255, 255, 255));
            EXPECT_EQ(at(4, 4), (std::array<std::uint8_t, 4>{ 0, 255, 0, 255 })) << "as rewritten";
        }

        /// A slot given back is taken over before the table grows.
        ///
        /// **A session opens and closes menus for hours**, and every window that opens makes
        /// textures. A table that only ever grew would be a slow leak with a number on it.
        TEST_F(RtxGuiDrawTest, aSlotGivenBackIsTakenOverBeforeTheTableGrows)
        {
            const std::uint32_t first = mRenderer->addGuiTexture(1, 1);
            const std::uint32_t second = mRenderer->addGuiTexture(1, 1);
            EXPECT_NE(first, second);

            mRenderer->dropGuiTexture(first);

            const std::uint32_t third = mRenderer->addGuiTexture(1, 1);
            EXPECT_EQ(third, first) << "the freed slot, not a new one";

            mRenderer->dropGuiTexture(second);
            mRenderer->dropGuiTexture(third);
        }

        /// A texture the table has just handed out is blank rather than whatever the memory held.
        TEST_F(RtxGuiDrawTest, aTextureIsBlankBeforeItIsWritten)
        {
            const std::uint32_t texture = mRenderer->addGuiTexture(1, 1);
            mHeld.push_back(texture);

            const std::uint32_t white = makeTexel({ 255, 255, 255, 255 });
            drawQuad(white, -1.0f, 1.0f, 1.0f, -1.0f, packColour(0, 0, 255, 255));

            // Nothing times anything is nothing: transparent black leaves the blue underneath.
            drawQuad(texture, -1.0f, 1.0f, 1.0f, -1.0f, packColour(255, 255, 255, 255));

            EXPECT_EQ(at(4, 4), (std::array<std::uint8_t, 4>{ 0, 0, 255, 255 }));
        }

        /// The GUI over a frame that was actually traced, which is the first time the two halves of
        /// this renderer meet.
        ///
        /// **Nothing here asserts a radiance.** What the trace makes of a bare wall is the trace's
        /// business and is asserted at length elsewhere; what matters is that the GUI lands on top
        /// of it and leaves the rest of the picture exactly as the trace left it.
        TEST_F(RtxGuiDrawTest, theGuiLandsOverATracedFrameAndLeavesTheRestOfItAlone)
        {
            constexpr std::array<std::uint32_t, 6> sQuadIndices{ 0, 1, 2, 0, 2, 3 };
            const std::array<osg::Vec3f, 4> sWall{
                osg::Vec3f(-8000.0f, 200.0f, -8000.0f),
                osg::Vec3f(8000.0f, 200.0f, -8000.0f),
                osg::Vec3f(8000.0f, 200.0f, 8000.0f),
                osg::Vec3f(-8000.0f, 200.0f, 8000.0f),
            };

            SceneDesc scene;
            scene.addInstance(MeshInstance{
                .mTransform = osg::Matrixf::identity(), .mMesh = scene.addMesh(sWall, {}, {}, sQuadIndices) });

            mRenderer->setScene(scene, {}, SeaState{});

            const Shaders::VisibilityConstants camera
                = makeCamera(osg::Vec3f(), osg::Vec3f(0.0f, 100.0f, 0.0f), 60.0f, sExtent, sExtent, 1000000.0f);

            mRenderer->renderFrame(camera, FrameOptions{});
            const std::array<std::uint8_t, 4> traced = at(sExtent - 2, 4);

            // The same camera and the same scene, so the same picture — and then the GUI over half
            // of it.
            mRenderer->renderFrame(camera, FrameOptions{});

            const std::uint32_t texture = makeTexel({ 17, 34, 51, 255 });
            drawQuad(texture, -1.0f, 1.0f, 0.0f, -1.0f, packColour(255, 255, 255, 255));

            EXPECT_EQ(at(1, 4), (std::array<std::uint8_t, 4>{ 17, 34, 51, 255 })) << "where the GUI drew";
            EXPECT_EQ(at(sExtent - 2, 4), traced) << "where it did not";
        }

        /// Nothing to draw is not an error and does not touch the frame.
        TEST_F(RtxGuiDrawTest, anEmptyGuiLeavesTheFrameAlone)
        {
            const std::uint32_t white = makeTexel({ 255, 255, 255, 255 });
            drawQuad(white, -1.0f, 1.0f, 1.0f, -1.0f, packColour(17, 34, 51, 255));

            mRenderer->drawGui({}, {});

            EXPECT_EQ(at(4, 4), (std::array<std::uint8_t, 4>{ 17, 34, 51, 255 }));
        }
    }
}
