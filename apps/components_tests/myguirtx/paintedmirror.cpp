#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/ref_ptr>

#include <apps/components_tests/rtx/support/countingrenderer.hpp>
#include <components/myguirtx/paintedmirror.hpp>
#include <components/rtx/renderer/guirenderer.hpp>
#include <components/sceneutil/imageregion.hpp>
#include <components/sceneutil/paintedtexture.hpp>

namespace MyGUIRtx
{
    namespace
    {
        osg::ref_ptr<osg::Image> makePicture(const int width, const int height)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            for (unsigned int at = 0; at < image->getTotalSizeInBytes(); ++at)
                image->data()[at] = static_cast<std::uint8_t>(at);
            return image;
        }

        std::array<std::uint32_t, 4> cornersOf(const Rtx::GuiRegion& region)
        {
            return { region.mX, region.mY, region.mWidth, region.mHeight };
        }

        /// **A painted picture is sent whole on its first draw, then the rectangles painted**, and
        /// nothing on a draw with no paint. The rectangle's rows go as they stand in the picture: a
        /// 2-by-2 rectangle at (1, 1) of a 4-wide picture is its bytes 20 to 27 and 36 to 43.
        TEST(RtxPaintedMirrorTest, thePictureGoesWholeAndThenThePaintedRectangle)
        {
            Rtx::Testing::CountingRenderer renderer;
            const osg::ref_ptr<SceneUtil::PaintedTexture> painted = new SceneUtil::PaintedTexture(makePicture(4, 3));

            PaintedMirror mirror(renderer, *painted);
            ASSERT_EQ(renderer.mLent.size(), 1u);
            EXPECT_EQ(cornersOf(renderer.mLent[0]), (std::array<std::uint32_t, 4>{ 0, 0, 4, 3 }));
            EXPECT_EQ(renderer.mGuiSent, 1u);

            mirror.refresh();
            EXPECT_EQ(renderer.mLent.size(), 1u) << "a draw with nothing painted sent something";

            painted->paint(SceneUtil::ImageRegion{ .mX = 1, .mY = 1, .mWidth = 2, .mHeight = 2 });
            mirror.refresh();
            ASSERT_EQ(renderer.mLent.size(), 2u);
            EXPECT_EQ(cornersOf(renderer.mLent[1]), (std::array<std::uint32_t, 4>{ 1, 1, 2, 2 }));
            EXPECT_EQ(renderer.mLending,
                (std::vector<std::uint8_t>{ 20, 21, 22, 23, 24, 25, 26, 27, 36, 37, 38, 39, 40, 41, 42, 43 }));
            EXPECT_EQ(renderer.mGuiSent, 2u);
        }

        /// **A size the renderer refused draws nothing and is asked for once**: a global map wider
        /// than the device's side. Neither a draw nor a paint after it lends or sends anything, and
        /// no draw asks for the slot again.
        TEST(RtxPaintedMirrorTest, aRefusedSizeDrawsNothingAndIsAskedForOnce)
        {
            Rtx::Testing::CountingRenderer renderer;
            renderer.mGuiSide = 2;
            const osg::ref_ptr<SceneUtil::PaintedTexture> painted = new SceneUtil::PaintedTexture(makePicture(4, 3));

            PaintedMirror mirror(renderer, *painted);
            EXPECT_TRUE(mirror.getSlot().isNone());
            mirror.refresh();
            painted->paint(SceneUtil::ImageRegion{ .mX = 1, .mY = 1, .mWidth = 2, .mHeight = 2 });
            mirror.refresh();

            EXPECT_EQ(renderer.mGuiAsked, 1u) << "the refused size was asked for again";
            EXPECT_TRUE(renderer.mLent.empty()) << "a refused slot was lent";
            EXPECT_EQ(renderer.mGuiSent, 0u) << "a refused slot was sent";
        }
    }
}
