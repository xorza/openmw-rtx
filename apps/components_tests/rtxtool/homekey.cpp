#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <apps/openmw/mwrender/rtx/framereport.hpp>
#include <apps/rtxtool/homekey.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/run.hpp>
#include <components/rtx/frameextents.hpp>
#include <components/rtx/renderer.hpp>
#include <components/rtx/texels.hpp>
#include <components/testing/util.hpp>

#include "../rtx/support/pngtext.hpp"

namespace RtxTool
{
    namespace
    {
        constexpr std::array<std::uint8_t, 8> sPixels{ 255, 0, 0, 255, 0, 128, 255, 64 };

        /// A note that says which frame it was taken on, in its hour.
        Stop noteOf(const std::uint64_t frame)
        {
            return Stop{ .mName = "pier",
                .mStand = { .mCell = "-2,-10", .mEye = osg::Vec3f(1, 2, 3), .mLook = osg::Vec3f(1, 1002, 3) },
                .mSky = { .mHour = static_cast<float>(frame), .mDay = 1, .mWeather = "Clear" } };
        }

        /// Frame `frame`, carrying the device's answer for `answered` where it has one, with the
        /// picture where `copied`.
        MWRender::FrameReport frameOf(
            const std::uint64_t frame, const std::optional<std::uint64_t> answered = {}, const bool copied = false)
        {
            MWRender::FrameReport report;
            report.mFrame = frame;
            if (answered.has_value())
            {
                report.mResult.emplace();
                report.mResult->mFrame = *answered;
                if (copied)
                    report.mResult->mPixels = sPixels;
            }
            return report;
        }

        Rtx::FrameExtents twoByOne()
        {
            Rtx::FrameExtents extents;
            extents.mOutputWidth = 2;
            extents.mOutputHeight = 1;
            return extents;
        }

        /// In text mode, as the keys are written and as `BlockFile` reads them: on Windows each line
        /// ends `\r\n` on disk, which a raw read would compare against the `\n` the test expects.
        std::string readText(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        std::string descriptionOf(const std::filesystem::path& path)
        {
            const std::vector<Rtx::Testing::PngText> texts = Rtx::Testing::readPngTexts(path);
            return texts.size() == 1 && texts[0].mKey == "Description" ? texts[0].mText : std::string();
        }

        /// A press keeps the note of the frame it asked the device to copy, and the picture of that
        /// same frame, whenever the copy comes back. A second press while the first copy is on its
        /// way waits for it rather than taking the note from under it, then keeps the first frame
        /// after; the pictures are numbered in the order of the presses, and each press appends its
        /// own note to the keys.
        TEST(RtxToolHomeKeyTest, aPressKeepsTheFrameItAskedForAndAPressInFlightWaitsForIt)
        {
            const std::filesystem::path dir = TestingOpenMW::currentTestDirPath();
            const std::filesystem::path keys = dir / "keys.cfg";
            const std::filesystem::path pictures = dir / "pictures";
            const Rtx::FrameExtents extents = twoByOne();

            {
                HomeKey home(keys, pictures);
                EXPECT_FALSE(home.wantsPicture());

                home.press();
                ASSERT_TRUE(home.wantsPicture());
                Stop note = noteOf(10);
                home.answer(&note, frameOf(10), extents);
                EXPECT_FALSE(home.wantsPicture()) << "one press, one frame";

                home.press();
                EXPECT_FALSE(home.wantsPicture()) << "a press while frame 10's copy is on its way";
                note = noteOf(11);
                home.answer(&note, frameOf(11, 9), extents);
                EXPECT_FALSE(home.wantsPicture());
                note = noteOf(12);
                home.answer(&note, frameOf(12, 10, true), extents);

                ASSERT_TRUE(home.wantsPicture()) << "frame 10 came back, so the second press may ask";
                note = noteOf(13);
                home.answer(&note, frameOf(13, 11), extents);
                EXPECT_FALSE(home.wantsPicture());
                note = noteOf(14);
                home.answer(&note, frameOf(14, 12), extents);
                note = noteOf(15);
                home.answer(&note, frameOf(15, 13, true), extents);
            }

            EXPECT_EQ(descriptionOf(pictures / "pier-1.png"), describeStanding(noteOf(10)));
            EXPECT_EQ(descriptionOf(pictures / "pier-2.png"), describeStanding(noteOf(13)));
            EXPECT_FALSE(std::filesystem::exists(pictures / "pier-3.png"));
            EXPECT_EQ(Rtx::readPng(pictures / "pier-1.png").mPixels,
                std::vector<std::uint8_t>(sPixels.begin(), sPixels.end()));
            EXPECT_EQ(readText(keys), "\n" + describeKey(noteOf(10)) + "\n" + describeKey(noteOf(13)));
        }

        /// A picture already in the directory is never written over: the next press takes the next
        /// free number. A press whose frame the device answered past, or answered without a copy,
        /// writes nothing — and the next press is not held up by it.
        TEST(RtxToolHomeKeyTest, aPictureNeverOverwritesAndACopyThatDidNotComeIsNoPicture)
        {
            const std::filesystem::path dir = TestingOpenMW::currentTestDirPath();
            const std::filesystem::path pictures = dir / "pictures";
            const Rtx::FrameExtents extents = twoByOne();

            std::filesystem::create_directories(pictures);
            std::ofstream(pictures / "pier-1.png") << "somebody else's";

            {
                HomeKey home({}, pictures);
                Stop note = noteOf(5);

                home.press();
                home.answer(&note, frameOf(5), extents);
                home.answer(&note, frameOf(6, 7), extents);
                EXPECT_FALSE(std::filesystem::exists(pictures / "pier-2.png")) << "frame 5 was answered past";

                home.press();
                ASSERT_TRUE(home.wantsPicture());
                note = noteOf(8);
                home.answer(&note, frameOf(8), extents);
                home.answer(&note, frameOf(9, 8), extents);

                home.press();
                ASSERT_TRUE(home.wantsPicture()) << "a copy that did not come holds nothing up";
                note = noteOf(10);
                home.answer(&note, frameOf(10), extents);
                home.answer(&note, frameOf(11, 10, true), extents);
            }

            EXPECT_EQ(readText(pictures / "pier-1.png"), "somebody else's");
            EXPECT_EQ(descriptionOf(pictures / "pier-2.png"), describeStanding(noteOf(10)));
            EXPECT_FALSE(std::filesystem::exists(pictures / "pier-3.png"));
        }
    }
}
