#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <apps/rtxtool/instruments/cardwatch.hpp>
#include <apps/rtxtool/instruments/gpuclock.hpp>
#include <apps/rtxtool/model/benchrecord.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/scene/mesh.hpp>

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
        /// **The host's geometry as placed, resting and holes**: of 1,600,000 vertices reached 1,500,000 stand in runs,
        /// 300,000 of them resting, so 1.20 M placed and 0.10 M in holes; of 3,000,000 indices 2,400,000 stand, 600,000
        /// resting: 1.80 M placed, 0.60 M in holes. The geometry's 52 MiB beside them.
        TEST(RtxBenchRecordTest, theHostLineSaysTheGeometryAsPlacedRestingAndHoles)
        {
            constexpr std::uint64_t sMiB = std::uint64_t{ 1 } << 20;
            const Rtx::ContentMemory content{ .mVertexEnd = 1'600'000,
                .mVerticesUsed = 1'500'000,
                .mIndexEnd = 3'000'000,
                .mIndicesUsed = 2'400'000,
                .mRestingVertices = 300'000,
                .mRestingIndices = 600'000,
                .mGeometryBytes = 52 * sMiB,
                .mReader = Rtx::ReaderMemory{ .mLentModels = 3,
                    .mSpareModels = 4,
                    .mFiledModels = 5,
                    .mLentBytes = 2 * sMiB,
                    .mSpareBytes = 6 * sMiB,
                    .mFiledBytes = 1 * sMiB,
                    .mLentContentBytes = 3 * sMiB / 2,
                    .mFiledContentBytes = sMiB / 4 },
                .mRing = {} };
            const std::string line = describeHostHeld(content);
            EXPECT_EQ(line.substr(0, line.find("   reader")),
                "  host  vertices 1.20 M placed, 0.30 M resting, 0.10 M in holes of 1.60 M reached   indices 1.80 M, "
                "0.60 M, 0.60 M of 3.00 M   geometry 52.0 MiB")
                << line;
            // The reader's room beside what its lent and filed models fill of it.
            EXPECT_NE(line.find("   reader 2.0 MiB lent in 3 models holding 1.5, 6.0 MiB spare in 4, 1.0 MiB filed "
                                "unlent in 5 holding 0.2\n"),
                std::string::npos)
                << line;
        }

        /// **The texel asks by why, with their bytes**, and nothing where no header was asked.
        TEST(RtxBenchRecordTest, theTexelLineSaysEveryAskByWhy)
        {
            constexpr std::uint64_t sMiB = std::uint64_t{ 1 } << 20;
            Rtx::TexelReads reads;
            EXPECT_EQ(describeTexelReads(reads), "") << "a renderer that asked no header";
            reads.mCounts = { 3, 40, 2, 0 };
            reads.mBytes = { 1 * sMiB, 120 * sMiB, 6 * sMiB, 0 };
            reads.mChainsFoundWhole = 17;
            reads.mCancelled = 5;
            EXPECT_EQ(describeTexelReads(reads),
                "  texels asked   fact 3 (1.0 MiB)   file 40 (120.0 MiB)   partner 2 (6.0 MiB)   preparation 0 (0.0 "
                "MiB)   chains found whole 17   cancelled 5\n");
        }

        /// **The ring by distance, what stands of what it holds**, only the distances holding a placement, the last
        /// one every cell past it.
        TEST(RtxBenchRecordTest, theRingLineSaysWhatStandsOfWhatIsHeldByDistance)
        {
            Rtx::RingStanding ring;
            EXPECT_EQ(describeRingStanding(ring), "");
            ring.mByDistance[1] = Rtx::RingDistance{ .mPlacements = 900,
                .mPlacementsStanding = 850,
                .mMeshes = 300,
                .mMeshesStanding = 290,
                .mVertices = 1'200'000,
                .mVerticesStanding = 1'150'000 };
            ring.mByDistance[Rtx::sRingDistances - 1] = Rtx::RingDistance{ .mPlacements = 40,
                .mPlacementsStanding = 2,
                .mMeshes = 30,
                .mMeshesStanding = 1,
                .mVertices = 90'000,
                .mVerticesStanding = 10'000 };
            EXPECT_EQ(describeRingStanding(ring),
                "  ring  standing of held by distance   1: placements 850 of 900, meshes 290 of 300, 1.15 of 1.20 M "
                "vertices   15 and past: placements 2 of 40, meshes 1 of 30, 0.01 of 0.09 M vertices\n");
        }

        /// **The game's cell stores and the caches' starved looks**, and nothing where the game holds no cell.
        TEST(RtxBenchRecordTest, theGameLineSaysTheCellStoresAndTheStarvedLooks)
        {
            EXPECT_EQ(describeGameHeld(GameHeld{}), "");
            EXPECT_EQ(describeGameHeld(GameHeld{ .mLoadedCells = 412, .mPreloadedCells = 9, .mStarvedLooks = 31 }),
                "  game  cell stores 412 loaded, 9 preloaded   caches 31 looks for room finding all in use\n");
        }

        TEST(RtxBenchRecordTest, theRecordWritesEveryNameAsAJsonString)
        {
            BenchPlace place;
            place.mView = R"(say "hi")";
            place.mCell = R"(C:\Vivec)";
            place.mWeather = "Clear";
            place.mCard.mViewed = true;
            place.mCard.mHolders.push_back(CardHolder{ .mName = "tab\there", .mSamples = 1 });

            // **A line each, ending in its line break and holding no other**, a name's own written as its escape: what
            // lets a run add a place to its record without writing the record again.
            const std::string headerLine = recordHeaderLine(BenchHeader{ .mSuite = "a\nb" });
            const std::string placeLine = recordPlaceLine(place);
            constexpr std::string_view headerOpens = R"({"header": {"suite": "a\nb", "output": [0, 0], )";
            constexpr std::string_view headerCloses = R"("cacheBudget": null}})"
                                                      "\n";
            constexpr std::string_view placeOpens = R"({"place": {"view": "say \"hi\"", )";
            EXPECT_TRUE(headerLine.starts_with(headerOpens)) << headerLine;
            EXPECT_TRUE(headerLine.ends_with(headerCloses)) << headerLine;
            EXPECT_EQ(std::ranges::count(headerLine, '\n'), 1);
            EXPECT_TRUE(placeLine.starts_with(placeOpens)) << placeLine;
            EXPECT_TRUE(placeLine.ends_with("}}\n")) << placeLine;
            EXPECT_EQ(std::ranges::count(placeLine, '\n'), 1);
            const std::string json = headerLine + placeLine;

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

            const std::string json = recordPlaceLine(places[0]) + recordPlaceLine(places[1]);

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

        /// **Every fraction in the record is a JSON number or `null`**: a figure of no samples is not finite, which
        /// JSON has no number for and Python's `json` refuses as `nan`, and the record is read by both.
        TEST(RtxBenchRecordTest, aFigureThatIsNotFiniteIsWrittenAsNull)
        {
            std::array<BenchPlace, 1> places;
            places[0].mWallSeconds = std::numeric_limits<double>::quiet_NaN();
            places[0].mHitPercent = std::numeric_limits<double>::infinity();
            places[0].mTravelled = 0.5;
            BenchHeader header;
            header.mHugePageShare = std::numeric_limits<float>::quiet_NaN();

            const std::string json = recordHeaderLine(header) + recordPlaceLine(places[0]);

            EXPECT_NE(json.find(R"("wallSeconds": null, "hitPercent": null, )"), std::string::npos) << json;
            EXPECT_NE(json.find(R"("travelled": 0.5000, )"), std::string::npos) << json;
            EXPECT_NE(json.find(R"("hugePageShare": null,)"), std::string::npos) << json;
            for (const std::string_view word : { ": nan", ": -nan", ": inf", ": -inf" })
                EXPECT_EQ(json.find(word), std::string::npos) << word << " in " << json;
        }

        /// **The record writes the place's scene, its memory and its content whole**: every count of
        /// the scene's instances and what it holds, every heap the device stated, each figure under its
        /// own name, the uses, and every count of the mirror's content and its reader. Each a different
        /// number, so a field written under another's name is told apart.
        TEST(RtxBenchRecordTest, theRecordWritesThePlacesSceneMemoryAndContentWhole)
        {
            BenchPlace place;
            place.mScene = Rtx::SceneStats{ .mInstances = Rtx::InstanceCounts{ .mPlaced = 41,
                                                .mCutout = 42,
                                                .mWater = 43,
                                                .mMedium = 44,
                                                .mAdditive = 45,
                                                .mFirstPerson = 46,
                                                .mMapped = 47,
                                                .mModMapped = 65,
                                                .mRelief = 49 },
                .mInstanceSlots = 50,
                .mStructureBytes = 51,
                .mStructureLiveBytes = 52,
                .mTableBytes = 53,
                .mGeometryBytes = 66,
                .mWorkBytes = 54,
                .mCompactableBytes = 55,
                .mCompactableNowBytes = 56,
                .mStructureMovedBytes = 67,
                .mTextureMovedBytes = 68,
                .mRebuilt = 57,
                .mTextureCount = 58,
                .mTextureBytes = 59,
                .mTextureBytesBySource = Rtx::TextureSourceBytes{ .mFiles = 60,
                    .mSpriteBakes = 61,
                    .mGroundComposites = 62,
                    .mGroundGloss = 63 },
                .mReducedTextureCount = 64 };
            place.mDriver = Platform::Process::DriverMemory{
                .mTotalVram = 71, .mResidentVram = 72, .mTotalGtt = 73, .mResidentGtt = 74, .mResidentCpu = 75
            };
            place.mMemory.mHeapCount = 2;
            place.mMemory.mHeaps[0] = Rtx::HeapUse{
                .mSize = 1, .mBudget = 2, .mHeld = 3, .mReserved = 4, .mLive = 5, .mBlocks = 6, .mDeviceLocal = true
            };
            place.mMemory.mHeaps[1] = Rtx::HeapUse{ .mSize = 7 };
            place.mMemory.mUses = Rtx::MemoryUses{
                .mFrame = 10, .mFrameReserve = 11, .mEssential = 12, .mEssentialGrowth = 17, .mContent = 13
            };
            place.mContent = Rtx::ContentMemory{ .mVertexEnd = 21,
                .mVerticesUsed = 22,
                .mIndexEnd = 23,
                .mIndicesUsed = 24,
                .mRestingVertices = 40,
                .mRestingIndices = 41,
                .mVertexHoles = 43,
                .mIndexHoles = 44,
                .mGeometryBytes = 42,
                .mLayerEnd = 25,
                .mMaskEnd = 26,
                .mRigRunEnd = 27,
                .mInfluenceEnd = 28,
                .mOffsetEnd = 29,
                .mPoseEnd = 30,
                .mBindVertexEnd = 31,
                .mRestingTextures = 38,
                .mReader = Rtx::ReaderMemory{ .mLentModels = 32,
                    .mSpareModels = 33,
                    .mFiledModels = 34,
                    .mLentBytes = 35,
                    .mSpareBytes = 36,
                    .mFiledBytes = 37,
                    .mLentContentBytes = 61,
                    .mFiledContentBytes = 62 },
                .mRing = {} };
            place.mContent.mRing.mByDistance[1] = Rtx::RingDistance{ .mPlacements = 55,
                .mPlacementsStanding = 56,
                .mMeshes = 57,
                .mMeshesStanding = 58,
                .mVertices = 59,
                .mVerticesStanding = 60 };

            place.mTexelReads.mCounts = { 46, 47, 48, 49 };
            place.mTexelReads.mBytes = { 50, 51, 52, 53 };
            place.mTexelReads.mChainsFoundWhole = 54;
            place.mTexelReads.mCancelled = 66;
            place.mGame = GameHeld{ .mLoadedCells = 63, .mPreloadedCells = 64, .mStarvedLooks = 65 };

            const std::string json = recordPlaceLine(place);

            constexpr std::string_view scene
                = R"("scene": {"instances": 41, "instanceSlots": 50, "cutoutInstances": 42, "waterInstances": 43, )"
                  R"("mediumInstances": 44, "additiveInstances": 45, "firstPersonInstances": 46, )"
                  R"("mappedInstances": 47, "modMappedInstances": 65, "reliefInstances": 49, )"
                  R"("structureBytes": 51, )"
                  R"("structureLiveBytes": 52, "compactableBytes": 55, "compactableNowBytes": 56, )"
                  R"("structureMovedBytes": 67, "textureMovedBytes": 68, "rebuilt": 57, )"
                  R"("tableBytes": 53, "geometryBytes": 66, "workBytes": 54, "textureCount": 58, "textureBytes": 59, )"
                  R"("reducedTextureCount": 64, "textureBytesBySource": {"files": 60, "spriteBakes": 61, )"
                  R"("groundComposites": 62, "groundGloss": 63}})";
            constexpr std::string_view memory
                = R"("memory": {"heaps": [{"size": 1, "budget": 2, "held": 3, "reserved": 4, "live": 5, "blocks": 6, )"
                  R"("deviceLocal": true}, {"size": 7, "budget": 0, "held": 0, "reserved": 0, "live": 0, )"
                  R"("blocks": 0, "deviceLocal": false}], "uses": {"frame": 10, "frameReserve": 11, "essential": 12, )"
                  R"("essentialGrowth": 17, "content": 13}})";
            constexpr std::string_view content
                = R"("content": {"vertexEnd": 21, "verticesUsed": 22, "indexEnd": 23, "indicesUsed": 24, )"
                  R"("restingVertices": 40, "restingIndices": 41, "vertexHoles": 43, "indexHoles": 44, )"
                  R"("geometryBytes": 42, )"
                  R"("layerEnd": 25, "maskEnd": 26, "rigRunEnd": 27, "influenceEnd": 28, "offsetEnd": 29, )"
                  R"("poseEnd": 30, "bindVertexEnd": 31, "restingTextures": 38, )"
                  R"("reader": {"lentModels": 32, "spareModels": 33, )"
                  R"("filedModels": 34, "lentBytes": 35, "spareBytes": 36, "filedBytes": 37, )"
                  R"("lentContentBytes": 61, "filedContentBytes": 62}, "ring": [{"placements": 0, )"
                  R"("placementsStanding": 0, "meshes": 0, "meshesStanding": 0, "vertices": 0, "verticesStanding": 0}, )"
                  R"({"placements": 55, "placementsStanding": 56, "meshes": 57, "meshesStanding": 58, "vertices": 59, )"
                  R"("verticesStanding": 60}, {"placements": 0,)";
            EXPECT_NE(json.find(scene), std::string::npos) << json;
            EXPECT_NE(json.find(memory), std::string::npos) << json;
            EXPECT_NE(
                json.find(R"(, "driver": {"totalVram": 71, "residentVram": 72, "totalGtt": 73, "residentGtt": 74, )"
                          R"("residentCpu": 75}, "content": )"),
                std::string::npos)
                << json;
            EXPECT_NE(json.find(content), std::string::npos) << json;
            EXPECT_NE(json.find(R"(, "texelReads": {"fact": 46, "factBytes": 50, "file": 47, "fileBytes": 51, )"
                                R"("partner": 48, "partnerBytes": 52, "preparation": 49, "preparationBytes": 53, )"
                                R"("chainsFoundWhole": 54, "cancelled": 66}, "game": {"loadedCells": 63, )"
                                R"("preloadedCells": 64, )"
                                R"("starvedLooks": 65})"),
                std::string::npos)
                << json;
        }
    }
}
