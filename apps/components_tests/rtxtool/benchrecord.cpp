#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <apps/rtxtool/instruments/cardwatch.hpp>
#include <apps/rtxtool/instruments/gpuclock.hpp>
#include <apps/rtxtool/model/benchrecord.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        /// A crossing is the frame that dropped, whole: the count, how many rebuilt, the worst and
        /// the total are what the report reads, and nothing else is measured.
        TEST(RtxBenchRecordTest, crossingsCountTheFramesThatDropped)
        {
            Crossings crossings;
            crossings.add(false, 40.0);
            crossings.add(true, 250.0);
            crossings.add(false, 10.0);

            EXPECT_EQ(crossings.mCount, 3u);
            EXPECT_EQ(crossings.mRebuilds, 1u);
            EXPECT_EQ(crossings.mWorstMs, 250.0);
            EXPECT_EQ(crossings.mTotalMs, 300.0);
        }

        /// The arrivals count the frames that extended the scene and keep the three worst frames of
        /// the run whatever they carried, longest first, so a reader can say whose the tail is.
        TEST(RtxBenchRecordTest, arrivalsCountExtendingFramesAndKeepTheThreeWorst)
        {
            Arrivals arrivals;
            Rtx::FrameSpend spend;
            spend.at(Rtx::Timing::Place) = 2.0;
            spend.at(Rtx::Timing::Upload) = 1.9;
            spend.at(Rtx::Timing::Finish) = 1.5;
            spend.at(Rtx::Timing::Wait) = 1.4;
            spend.at(Rtx::Timing::Walk) = 0.5;
            // The limiter's sleep is the largest figure of the frame and a share of `update`, so it
            // is not among the stretches printed.
            spend.at(Rtx::Timing::Update) = 4.0;
            spend.at(Rtx::Timing::Sleep) = 3.5;

            const auto frameOf = [&](const double frameMs) {
                Rtx::FrameSpend frame = spend;
                frame.at(Rtx::Timing::Frame) = frameMs;
                return frame;
            };
            arrivals.add(0, frameOf(5.0));
            arrivals.add(12, frameOf(9.0));
            arrivals.add(0, frameOf(4.0));
            arrivals.add(3, frameOf(7.0));
            arrivals.add(0, frameOf(6.0));

            EXPECT_EQ(arrivals.mFrames, 2u);
            EXPECT_EQ(arrivals.mMeshes, 15u);
            EXPECT_EQ(arrivals.mWorstMs, 9.0);
            EXPECT_EQ(arrivals.getMeanMs(), 8.0) << "(9 + 7) / 2";

            ASSERT_EQ(arrivals.mWorstCount, 3u);
            EXPECT_EQ(arrivals.mWorst[0].getFrameMs(), 9.0);
            EXPECT_EQ(arrivals.mWorst[0].mArrivedMeshes, 12u);
            EXPECT_EQ(arrivals.mWorst[1].getFrameMs(), 7.0);
            EXPECT_EQ(arrivals.mWorst[2].getFrameMs(), 6.0);
            EXPECT_EQ(arrivals.mWorst[2].mArrivedMeshes, 0u);

            BenchPlace place;
            place.mView = "still";
            place.mFrames = 5;
            place.mWallSeconds = 1.0;
            place.mArrivals = arrivals;
            const std::string described = describePlace(place);
            EXPECT_NE(described.find("2 frames extended the scene with 15 meshes"), std::string::npos) << described;
            // The three largest stretches, and never a share beside its whole: `upload` is most of
            // `place`, `wait` most of `finish` and `sleep` most of `update`, so `update` leads and
            // `finish` is the third.
            EXPECT_NE(described.find("9.0 ms (12 meshes: update 4.0 place 2.0 finish 1.5)"), std::string::npos)
                << described;
        }

        /// A route flown short says so beside its crossings, and one that arrived says nothing.
        TEST(RtxBenchRecordTest, thePlaceSaysHowMuchOfARouteWasFlownOnlyWhereItEndedShort)
        {
            BenchPlace place;
            place.mView = "route";
            place.mFrames = 1;
            place.mWallSeconds = 1.0;
            place.mCrossings.add(true, 100.0);

            EXPECT_EQ(describePlace(place).find("of the route flown"), std::string::npos);

            place.mTravelled = 0.25;
            EXPECT_NE(describePlace(place).find("25% of the route flown"), std::string::npos);
        }

        /// A place whose textures stand smaller than their files says how many on the scene's line,
        /// and one drawn as its files are says nothing about it.
        TEST(RtxBenchRecordTest, thePlaceSaysHowManyTexturesStandSmallerOnlyWhereAnyDo)
        {
            BenchPlace place;
            place.mView = "town";
            place.mCell = "-3,-2";
            place.mFrames = 1;
            place.mWallSeconds = 1.0;
            place.mScene.mTextureCount = 737;

            EXPECT_EQ(describePlace(place).find("smaller"), std::string::npos) << describePlace(place);

            place.mScene.mReducedTextureCount = 212;
            EXPECT_NE(describePlace(place).find("737 textures, 0.0 MiB, 212 of them held smaller"), std::string::npos)
                << describePlace(place);
        }

        /// A name is written into the record as a JSON string, whatever it holds: a view, a cell,
        /// a suite and a process holding the card are all somebody else's text, and a quote left
        /// bare in one ended the record there.
        TEST(RtxBenchRecordTest, theRecordWritesEveryNameAsAJsonString)
        {
            BenchPlace place;
            place.mView = R"(say "hi")";
            place.mCell = R"(C:\Vivec)";
            place.mWeather = "Clear";
            place.mCard.mViewed = true;
            place.mCard.mHolders.push_back(CardHolder{ .mName = "tab\there", .mSamples = 1 });

            const std::filesystem::path path = TestingOpenMW::outputFilePath("escaped-record.json");
            ASSERT_TRUE(writeJson(path, BenchHeader{ .mSuite = "a\nb" }, std::span(&place, 1)).isOk());

            std::ostringstream read;
            read << std::ifstream(path).rdbuf();
            const std::string json = read.str();
            std::filesystem::remove(path);

            // Named outside the assertions: MSVC's preprocessor reads a raw string's `\"` inside a
            // macro argument as the end of the literal.
            constexpr std::string_view quote = R"("view": "say \"hi\"")";
            constexpr std::string_view backslash = R"("cell": "C:\\Vivec")";
            constexpr std::string_view newline = R"("suite": "a\nb")";
            constexpr std::string_view tab = R"({"name": "tab\there")";

            EXPECT_NE(json.find(quote), std::string::npos) << json;
            EXPECT_NE(json.find(backslash), std::string::npos) << json;
            EXPECT_NE(json.find(newline), std::string::npos) << json;
            EXPECT_NE(json.find(tab), std::string::npos) << json;
        }

        /// The clock goes into the record with the mean and the count a frame time is read against,
        /// so two records say whether they ran at one clock; and what the instrument could not
        /// read is null, not nought. Core (1785 + 2070 + 1980) / 3 = 1945 over 3, memory
        /// (9001 + 8000) / 2 = 8500 over 2.
        ///
        /// **And the frame thread's counts, every one of them**, which the report's line reads as
        /// ratios: null where nothing was counted.
        TEST(RtxBenchRecordTest, theRecordWritesTheClockAndTheThreadsCountsAsTheReportReadsThem)
        {
            std::array<BenchPlace, 2> places;
            BenchPlace& place = places[0];
            place.mClock = GpuClock::reading(1785, 9001, std::nullopt, std::nullopt);
            place.mClock.add(GpuClock::reading(2070, 8000, std::nullopt, std::nullopt));
            place.mClock.add(GpuClock::reading(1980, std::nullopt, std::nullopt, std::nullopt));
            place.mThread = ThreadCounts{ .mCycles = 5000,
                .mInstructions = 8000,
                .mCacheMisses = 24,
                .mRunningNs = 1000,
                .mEfficiencyNs = 250,
                .mCounted = 0.5,
                .mKernelCounted = true,
                .mTwoKinds = true,
                .mRead = true };

            const std::filesystem::path path = TestingOpenMW::outputFilePath("clock-record.json");
            ASSERT_TRUE(writeJson(path, BenchHeader{}, places).isOk());

            std::ostringstream read;
            read << std::ifstream(path).rdbuf();
            const std::string json = read.str();
            std::filesystem::remove(path);

            constexpr std::string_view clock = R"("clock": {"core": {"meanMhz": 1945, "lowestMhz": 1785, )"
                                               R"("highestMhz": 2070, "readings": 3}, "memory": {"meanMhz": 8500, )"
                                               R"("lowestMhz": 8000, "highestMhz": 9001, "readings": 2}, )"
                                               R"("temperatureC": null, "throttle": null})";
            EXPECT_NE(json.find(clock), std::string::npos) << json;

            constexpr std::string_view counted = R"("thread": {"cycles": 5000, "instructions": 8000, )"
                                                 R"("cacheMisses": 24, "runningNs": 1000, "efficiencyNs": 250, )"
                                                 R"("counted": 0.5000, "kernelCounted": true, "twoKinds": true}})";
            constexpr std::string_view uncounted = R"("thread": null})";
            EXPECT_NE(json.find(counted), std::string::npos) << json;
            EXPECT_NE(json.find(uncounted), std::string::npos) << json;
        }

        /// **The record writes the place's memory and its content whole**: every heap the device
        /// stated, each figure under its own name, the host-written pair and the uses, and every
        /// count of the mirror's content and its reader. Each a different number, so a field
        /// written under another's name is told apart.
        TEST(RtxBenchRecordTest, theRecordWritesThePlacesMemoryAndContentWhole)
        {
            BenchPlace place;
            place.mMemory.mHeapCount = 2;
            place.mMemory.mHeaps[0] = Rtx::HeapUse{ .mSize = 1,
                .mBudget = 2,
                .mHeld = 3,
                .mReserved = 4,
                .mLive = 5,
                .mBlocks = 6,
                .mDeviceLocal = true,
                .mHostVisible = false };
            place.mMemory.mHeaps[1] = Rtx::HeapUse{ .mSize = 7, .mHostVisible = true };
            place.mMemory.mHostWrittenReserved = 8;
            place.mMemory.mHostWrittenLive = 9;
            place.mMemory.mUses = Rtx::MemoryUses{ .mFrame = 10,
                .mFrameReserve = 11,
                .mEssential = 12,
                .mStructures = 13,
                .mStructureRoom = 14,
                .mTextures = 15,
                .mTextureRoom = 16 };
            place.mContent = Rtx::ContentMemory{ .mVertexEnd = 21,
                .mVerticesUsed = 22,
                .mIndexEnd = 23,
                .mIndicesUsed = 24,
                .mLayerEnd = 25,
                .mMaskEnd = 26,
                .mRigRunEnd = 27,
                .mInfluenceEnd = 28,
                .mOffsetEnd = 29,
                .mPoseEnd = 30,
                .mBindVertexEnd = 31,
                .mReader = Rtx::ReaderMemory{ .mLentModels = 32,
                    .mSpareModels = 33,
                    .mFiledModels = 34,
                    .mLentBytes = 35,
                    .mSpareBytes = 36,
                    .mFiledBytes = 37 } };

            const std::filesystem::path path = TestingOpenMW::outputFilePath("memory-record.json");
            ASSERT_TRUE(writeJson(path, BenchHeader{}, std::span(&place, 1)).isOk());
            std::ostringstream read;
            read << std::ifstream(path).rdbuf();
            const std::string json = read.str();
            std::filesystem::remove(path);

            constexpr std::string_view memory
                = R"("memory": {"heaps": [{"size": 1, "budget": 2, "held": 3, "reserved": 4, "live": 5, "blocks": 6, )"
                  R"("deviceLocal": true, "hostVisible": false}, {"size": 7, "budget": 0, "held": 0, "reserved": 0, )"
                  R"("live": 0, "blocks": 0, "deviceLocal": false, "hostVisible": true}], "hostWrittenReserved": 8, )"
                  R"("hostWrittenLive": 9, "uses": {"frame": 10, "frameReserve": 11, "essential": 12, )"
                  R"("structures": 13, "structureRoom": 14, "textures": 15, "textureRoom": 16}})";
            constexpr std::string_view content
                = R"("content": {"vertexEnd": 21, "verticesUsed": 22, "indexEnd": 23, "indicesUsed": 24, )"
                  R"("layerEnd": 25, "maskEnd": 26, "rigRunEnd": 27, "influenceEnd": 28, "offsetEnd": 29, )"
                  R"("poseEnd": 30, "bindVertexEnd": 31, "reader": {"lentModels": 32, "spareModels": 33, )"
                  R"("filedModels": 34, "lentBytes": 35, "spareBytes": 36, "filedBytes": 37}})";
            EXPECT_NE(json.find(memory), std::string::npos) << json;
            EXPECT_NE(json.find(content), std::string::npos) << json;
        }
    }
}
