#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/pngtext.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/renderer/png.hpp>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        /// The CRC the format specifies for the empty `IEND` chunk, published with it, so the test's
        /// own CRC is the specification's before it judges anybody else's.
        TEST(RtxPngTest, theChunkCrcIsTheOneThePngSpecificationPublishes)
        {
            EXPECT_EQ(Testing::crcOf("IEND"), 0xAE426082u);
        }

        /// A picture written with a description carries it whole, UTF-8 and all, as the one text
        /// chunk of the file, under the keyword viewers show; and the pixels are the pixels a
        /// picture written without one has. Written without one, the file carries no text.
        TEST(RtxPngTest, aDescriptionTravelsInsideThePictureAndLeavesItsPixelsAlone)
        {
            constexpr std::array<std::uint8_t, 8> pixels{ 255, 0, 0, 255, 0, 128, 255, 64 };
            const std::string description = "# -2,-10 at -9491, -74261, 450 — bearing 265°\n[view]\npos = 1, 2, 3\n";

            const std::filesystem::path plain = TestingOpenMW::outputFilePath("plain.png");
            const std::filesystem::path described = TestingOpenMW::outputFilePath("described.png");
            ASSERT_TRUE(writePng(plain, 2, 1, pixels).isOk());
            ASSERT_TRUE(writePng(described, 2, 1, pixels, description).isOk());

            EXPECT_TRUE(Testing::readPngTexts(plain).empty());

            const std::vector<Testing::PngText> texts = Testing::readPngTexts(described);
            ASSERT_EQ(texts.size(), 1u);
            EXPECT_EQ(texts[0].mKey, "Description");
            EXPECT_EQ(texts[0].mText, description);

            const Misc::Result<PngImage, std::string> read = readPng(described);
            ASSERT_TRUE(read.isOk()) << read.error();
            EXPECT_EQ(read.value().mWidth, 2u);
            EXPECT_EQ(read.value().mHeight, 1u);
            std::vector<std::uint16_t> widened;
            for (const std::uint8_t level : pixels)
                widened.push_back(static_cast<std::uint16_t>(level * sSamplesPerLevel));
            EXPECT_EQ(read.value().mSamples, widened);
        }

        /// **A sixteen-bit picture reads back sample for sample**, the ends of the scale, the middle
        /// and a sample beside a byte's level included, with its description; and an eight-bit one
        /// reads on the same scale, a level `v` as `257 v`, which the test above holds.
        TEST(RtxPngTest, aSixteenBitPictureReadsBackSampleForSample)
        {
            constexpr std::array<std::uint16_t, 8> samples{ 0, 1, 32768, 65535, 257, 258, 25700, 25829 };

            const std::filesystem::path deep = TestingOpenMW::outputFilePath("deep.png");
            ASSERT_TRUE(writePng(deep, 2, 1, samples, "a mean").isOk());

            const Misc::Result<PngImage, std::string> read = readPng(deep);
            ASSERT_TRUE(read.isOk()) << read.error();
            EXPECT_EQ(read.value().mWidth, 2u);
            EXPECT_EQ(read.value().mHeight, 1u);
            EXPECT_EQ(read.value().mSamples, std::vector<std::uint16_t>(samples.begin(), samples.end()));

            const std::vector<Testing::PngText> texts = Testing::readPngTexts(deep);
            ASSERT_EQ(texts.size(), 1u);
            EXPECT_EQ(texts[0].mText, "a mean");
        }

        /// What could not be written or read says which file and what was wrong with it, each of the
        /// three ways a read fails apart.
        TEST(RtxPngTest, aFailedWriteOrReadSaysWhichFileAndWhy)
        {
            constexpr std::array<std::uint8_t, 4> pixel{ 1, 2, 3, 255 };

            const std::filesystem::path nowhere = TestingOpenMW::outputFilePath("no-such-dir") / "picture.png";
            EXPECT_EQ(writePng(nowhere, 1, 1, pixel).error(), "cannot write " + Files::pathToUnicodeString(nowhere));
            EXPECT_EQ(writePng(nowhere, 1, 1, pixel, "described").error(),
                "cannot write " + Files::pathToUnicodeString(nowhere));

            EXPECT_EQ(readPng(nowhere).error(), Files::pathToUnicodeString(nowhere) + " is missing");

            const std::filesystem::path text = TestingOpenMW::outputFilePath("text.png");
            std::ofstream(text) << "not a picture";
            EXPECT_EQ(readPng(text).error(), Files::pathToUnicodeString(text) + " does not decode");
        }
    }
}
