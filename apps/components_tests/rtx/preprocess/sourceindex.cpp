#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture> // IWYU pragma: keep: the S3TC formats, which Windows's gl.h lacks

#include <components/contentstore/key.hpp>
#include <components/resource/imageheader.hpp>
#include <components/rtx/image/imagefacts.hpp>
#include <components/rtx/image/specularlayout.hpp>
#include <components/rtx/preprocess/headerderived.hpp>
#include <components/rtx/preprocess/sourceindex.hpp>
#include <components/testing/util.hpp>

namespace Rtx
{
    namespace
    {
        using Clock = std::filesystem::file_time_type::clock;

        /// A record of a DXT1 file of 8 by 8 in three levels, under `stamp`, its facts told apart by it.
        SourceRecord recordOf(const std::uint64_t stamp)
        {
            return SourceRecord{ .mStamp = stamp,
                .mShape = Resource::HeaderShape{ .mWidth = 8,
                    .mHeight = 8,
                    .mDepth = 1,
                    .mInternalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                    .mPixelFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                    .mDataType = GL_UNSIGNED_BYTE,
                    .mPacking = 1,
                    .mRowLength = 0,
                    .mOrigin = osg::Image::TOP_LEFT,
                    .mMipmaps = { 32, 40 } },
                .mFacts = FileFacts{ .mFile = ContentStore::Key{ { stamp, stamp + 1 } },
                    .mFacts = ImageFacts{ .mReachesSolid = stamp % 2 == 0,
                        .mMean = MeanTexel{},
                        .mAlphaRange = AlphaRange{ .mLow = 3, .mHigh = static_cast<std::uint8_t>(stamp) },
                        .mSpecularLayout = SpecularLayout::Classic } } };
        }

        void expectSame(const std::optional<SourceRecord>& found, const SourceRecord& filed)
        {
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ(found->mStamp, filed.mStamp);
            EXPECT_EQ(found->mShape, filed.mShape);
            EXPECT_EQ(found->mFacts.mFile, filed.mFacts.mFile);
            EXPECT_EQ(found->mFacts.mFacts.mReachesSolid, filed.mFacts.mFacts.mReachesSolid);
            EXPECT_EQ(found->mFacts.mFacts.mAlphaRange, filed.mFacts.mFacts.mAlphaRange);
            EXPECT_EQ(found->mFacts.mFacts.mSpecularLayout, filed.mFacts.mFacts.mSpecularLayout);
        }

        /// A file time well before any index this test writes, and one well after.
        std::filesystem::file_time_type longAgo()
        {
            return Clock::now() - std::chrono::hours(24 * 365);
        }

        /// **What is filed is found by the next session at its path and stamp, as it was filed**: one path at two
        /// stamps and another at one, written, then read by another index on the same file: each record comes back
        /// whole, a stamp none was filed at finds nothing, and so does a path none was. Nothing moved since the
        /// read, so nothing is written again; a find on another day marks the record named, which is a change.
        TEST(RtxSourceIndexTest, whatIsFiledIsFoundByTheNextSessionAtItsPathAndStamp)
        {
            const std::filesystem::path path = TestingOpenMW::currentTestDirPath() / "sources.index";
            {
                SourceIndex index(path);
                EXPECT_EQ(index.size(), 0u);
                index.file("textures/a.dds", recordOf(10), 100);
                index.file("textures/a.dds", recordOf(11), 100);
                index.file("textures/b.dds", recordOf(20), 100);
                EXPECT_TRUE(index.isChanged());
                ASSERT_TRUE(index.write());
                EXPECT_FALSE(index.isChanged());
            }

            SourceIndex next(path);
            EXPECT_EQ(next.size(), 3u);
            expectSame(next.find("textures/a.dds", 10, longAgo(), 100), recordOf(10));
            expectSame(next.find("textures/a.dds", 11, longAgo(), 100), recordOf(11));
            expectSame(next.find("textures/b.dds", 20, longAgo(), 100), recordOf(20));
            EXPECT_FALSE(next.find("textures/a.dds", 12, longAgo(), 100).has_value()) << "another stamp";
            EXPECT_FALSE(next.find("textures/c.dds", 10, longAgo(), 100).has_value()) << "another path";
            EXPECT_FALSE(next.isChanged()) << "a find on the day the record was named";
            EXPECT_TRUE(next.find("textures/b.dds", 20, longAgo(), 101).has_value());
            EXPECT_TRUE(next.isChanged()) << "a record named on another day";
        }

        /// **A record of a file no older than the index's own write is not trusted**, as Git's racily clean entry: the
        /// index written, a file last written a second before the write is found, and one written at the write or
        /// after is not; nor is any record of an index that never stood on the disk.
        TEST(RtxSourceIndexTest, aRecordOfAFileNoOlderThanTheIndexIsNotTrusted)
        {
            const std::filesystem::path path = TestingOpenMW::currentTestDirPath() / "sources.index";
            {
                SourceIndex index(path);
                index.file("textures/a.dds", recordOf(10), 100);
                EXPECT_FALSE(index.find("textures/a.dds", 10, longAgo(), 100).has_value())
                    << "a record of an index never written";
                ASSERT_TRUE(index.write());
            }
            const std::filesystem::file_time_type written = std::filesystem::last_write_time(path);

            SourceIndex next(path);
            EXPECT_TRUE(next.find("textures/a.dds", 10, written - std::chrono::seconds(1), 100).has_value());
            EXPECT_FALSE(next.find("textures/a.dds", 10, written, 100).has_value()) << "written in the same tick";
            EXPECT_FALSE(next.find("textures/a.dds", 10, written + std::chrono::seconds(1), 100).has_value());
        }

        /// **An index that does not read whole is an empty one**: a byte of its body changed, which its digest finds,
        /// and the file cut short each read as no record, and the next write stands as the records filed after.
        TEST(RtxSourceIndexTest, anIndexThatDoesNotReadWholeIsEmpty)
        {
            const std::filesystem::path path = TestingOpenMW::currentTestDirPath() / "sources.index";
            {
                SourceIndex index(path);
                index.file("textures/a.dds", recordOf(10), 100);
                ASSERT_TRUE(index.write());
            }
            std::vector<char> bytes;
            {
                std::ifstream file(path, std::ios::binary);
                bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            }
            ASSERT_GT(bytes.size(), 40u);

            for (const bool cut : { false, true })
            {
                std::vector<char> broken = bytes;
                if (cut)
                    broken.resize(broken.size() - 1);
                else
                    broken[20] ^= 1;
                {
                    std::ofstream file(path, std::ios::binary | std::ios::trunc);
                    file.write(broken.data(), static_cast<std::streamsize>(broken.size()));
                }
                SourceIndex read(path);
                EXPECT_EQ(read.size(), 0u) << (cut ? "cut short" : "a byte changed");
            }
        }

        /// **A path holds a record a stamp, its least recently named dropped past four**, and a record no run named
        /// for thirty days goes: five stamps of one path, named on days 100 to 104, keep the last four, and one filed
        /// again at its stamp replaces its record; a drop on day 133 takes those named on days 101 and 102, more than
        /// thirty days before, and keeps 103 and 104.
        TEST(RtxSourceIndexTest, aPathHoldsARecordAStampToABound)
        {
            SourceIndex index(TestingOpenMW::currentTestDirPath() / "sources.index");
            for (std::uint32_t at = 0; at < 5; ++at)
                index.file("textures/a.dds", recordOf(10 + at), 100 + at);
            EXPECT_EQ(index.size(), 4u) << "the least recently named past four";

            index.file("textures/a.dds", recordOf(12), 102);
            EXPECT_EQ(index.size(), 4u) << "a stamp filed again is one record";

            index.dropUnnamed(133);
            EXPECT_EQ(index.size(), 2u) << "named on days 101 and 102 more than thirty days before 133";
            index.dropUnnamed(200);
            EXPECT_EQ(index.size(), 0u);
        }
    }
}
