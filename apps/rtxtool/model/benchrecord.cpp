#include "benchrecord.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <apps/openmw/mwrender/rtx/mirrorknobs.hpp>
#include <apps/rtxtool/instruments/jsontext.hpp>
#include <apps/rtxtool/instruments/reportfile.hpp>
#include <components/rtx/common/mebibytes.hpp>
#include <components/rtx/mirror/cells/readermemory.hpp>
#include <components/rtx/mirror/contentmemory.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/renderer/memoryreport.hpp>

namespace RtxTool
{
    namespace
    {
        std::string asJson(std::string_view text)
        {
            return jsonString(text);
        }

        /// Null where no reading read it.
        std::string asJson(const ClockRange& range)
        {
            if (range.mReadings == 0)
                return "null";

            return std::format(R"({{"meanMhz": {}, "lowestMhz": {}, "highestMhz": {}, "readings": {}}})",
                range.getMeanMhz(), range.mLowestMhz, range.mHighestMhz, range.mReadings);
        }

        /// Null where there is no value, and a fraction through `jsonNumber` like every other.
        template <class T>
        std::string asJson(const std::optional<T>& value)
        {
            if (!value.has_value())
                return "null";
            if constexpr (std::is_floating_point_v<T>)
                return jsonNumber(*value, 4);
            else
                return std::format("{}", *value);
        }

        /// Null where nothing answered, so a record from a machine with no driver library says it carries no clock
        /// rather than claiming one of zero; and null for each part the instrument cannot read. The mean and the
        /// count are in it because they say whether two records ran at one clock.
        std::string asJson(const GpuClock& clock)
        {
            if (!clock.mRead)
                return "null";

            const std::string throttle
                = clock.mThrottleMask.has_value() ? asJson(describeThrottle(*clock.mThrottleMask)) : "null";
            return std::format(R"({{"core": {}, "memory": {}, "temperatureC": {}, "throttle": {}}})",
                asJson(clock.mCore), asJson(clock.mMemory), asJson(clock.mTemperatureC), throttle);
        }

        /// The scale a fixed exposure states, or nothing for one measured or held.
        std::optional<float> fixedScaleOf(const Rtx::ExposureRule& exposure)
        {
            const Rtx::FixedExposure* fixed = std::get_if<Rtx::FixedExposure>(&exposure);
            return fixed != nullptr ? std::optional<float>(fixed->mScale) : std::nullopt;
        }

        /// What the renderer was made with, as the report's header has it, one key a premise.
        std::string asJson(const MWRender::RunSetup& setup, const std::optional<float>& step)
        {
            const Rtx::RenderProfile& profile = setup.mRun.mProfile;
            const MWRender::MirrorKnobs& mirror = setup.mMirror;
            return std::format(R"(  "filter": {}, "jitter": {}, "gamma": {}, "show": "{}", )"
                               R"("exposure": {}, "exposureHeld": {}, "variants": {}, "holdMs": {},)"
                               "\n"
                               R"(  "landCells": {}, "viewingDistance": {}, "distantStatics": {}, "step": {}, )"
                               R"("settled": {}, "memoryBudget": {}, "cacheBudget": {},)",
                profile.mReconstruction.mDenoise, profile.mReconstruction.mJitter, jsonNumber(profile.mGamma, 3),
                Rtx::sSurfaceViewNames.name(profile.mShow), asJson(fixedScaleOf(profile.mExposure)),
                std::holds_alternative<Rtx::HeldExposure>(profile.mExposure), profile.mSpecializeKernels,
                jsonNumber(profile.mStressOverlapMs, 3), jsonNumber(mirror.mReach.mCells, 1),
                jsonNumber(mirror.mReach.mViewingDistance, 1), mirror.mDistantStatics, asJson(step),
                asJson(setup.mSettled), asJson(setup.mRun.mMemoryBudget), asJson(setup.mCacheBudget));
        }

        /// Null where nothing looked, for the same reason; in the record at all because a run another process drew
        /// through is not the same run as one on another commit.
        std::string asJson(const CardShare& card)
        {
            if (!card.mViewed)
                return "null";

            std::string holders = "[";
            for (std::size_t at = 0; at < card.mHolders.size(); ++at)
                holders += std::format(R"({}{{"name": {}, "samples": {}}})", at == 0 ? "" : ", ",
                    asJson(card.mHolders[at].mName), card.mHolders[at].mSamples);
            holders += ']';

            return std::format(R"({{"seconds": {}, "samples": {}, "others": {}, "holders": {}}})",
                jsonNumber(card.mSeconds, 2), card.mSamples, card.mOthers, holders);
        }

        /// Null where nothing was counted, for the same reason; and every count, so a record read
        /// against another can tell a drift in the host rows from a layout that misses more.
        std::string asJson(const ThreadCounts& counts)
        {
            if (!counts.mRead)
                return "null";

            return std::format(
                R"({{"cycles": {}, "instructions": {}, "cacheMisses": {}, "runningNs": {}, "efficiencyNs": {}, "counted": {}, "kernelCounted": {}, "twoKinds": {}}})",
                counts.mCycles, counts.mInstructions, counts.mCacheMisses, counts.mRunningNs, counts.mEfficiencyNs,
                jsonNumber(counts.mCounted, 4), counts.mKernelCounted, counts.mTwoKinds);
        }

        /// Everything a scene came to, so the record can compare what a change cost in memory as well as in time.
        /// Every field, because the report beside it chooses and a record does not: it exists to be diffed against
        /// the same run on another commit, and a figure it never wrote is one nobody can go back for.
        std::string asJson(const Rtx::SceneStats& scene)
        {
            return std::format(R"({{"instances": {}, "instanceSlots": {}, "cutoutInstances": {}, )"
                               R"("waterInstances": {}, "mediumInstances": {}, "additiveInstances": {}, )"
                               R"("firstPersonInstances": {}, "mappedInstances": {}, "modMappedInstances": {}, )"
                               R"("reliefInstances": {}, )"
                               R"("structureBytes": {}, "structureLiveBytes": {}, )"
                               R"("compactableBytes": {}, "compactableNowBytes": {}, "structureMovedBytes": {}, )"
                               R"("textureMovedBytes": {}, "rebuilt": {}, )"
                               R"("tableBytes": {}, "geometryBytes": {}, "workBytes": {}, "textureCount": {}, )"
                               R"("textureBytes": {}, )"
                               R"("reducedTextureCount": {}, "textureBytesBySource": {{"files": {}, )"
                               R"("spriteBakes": {}, "groundComposites": {}, "groundGloss": {}}}}})",
                scene.mInstances.mPlaced, scene.mInstanceSlots, scene.mInstances.mCutout, scene.mInstances.mWater,
                scene.mInstances.mMedium, scene.mInstances.mAdditive, scene.mInstances.mFirstPerson,
                scene.mInstances.mMapped, scene.mInstances.mModMapped, scene.mInstances.mRelief, scene.mStructureBytes,
                scene.mStructureLiveBytes, scene.mCompactableBytes, scene.mCompactableNowBytes,
                scene.mStructureMovedBytes, scene.mTextureMovedBytes, scene.mRebuilt, scene.mTableBytes,
                scene.mGeometryBytes, scene.mWorkBytes, scene.mTextureCount, scene.mTextureBytes,
                scene.mReducedTextureCount, scene.mTextureBytesBySource.mFiles,
                scene.mTextureBytesBySource.mSpriteBakes, scene.mTextureBytesBySource.mGroundComposites,
                scene.mTextureBytesBySource.mGroundGloss);
        }

        /// Every heap the device stated and every use, as the report's memory lines read them.
        std::string asJson(const Rtx::MemoryReport& memory)
        {
            std::string heaps = "[";
            for (std::uint32_t at = 0; at < memory.mHeapCount; ++at)
            {
                const Rtx::HeapUse& heap = memory.mHeaps[at];
                heaps += std::format(R"({}{{"size": {}, "budget": {}, "held": {}, "reserved": {}, "live": {}, )"
                                     R"("blocks": {}, "deviceLocal": {}}})",
                    at == 0 ? "" : ", ", heap.mSize, heap.mBudget, heap.mHeld, heap.mReserved, heap.mLive, heap.mBlocks,
                    heap.mDeviceLocal);
            }
            heaps += ']';

            const Rtx::MemoryUses& uses = memory.mUses;
            return std::format(R"({{"heaps": {}, )"
                               R"("uses": {{"frame": {}, "frameReserve": {}, "essential": {}, "essentialGrowth": {}, )"
                               R"("structures": {}, "structureGrowth": {}, "structureRoom": {}, "textures": {}, )"
                               R"("textureRoom": {}}}}})",
                heaps, uses.mFrame, uses.mFrameReserve, uses.mEssential, uses.mEssentialGrowth, uses.mStructures,
                uses.mStructureGrowth, uses.mStructureRoom, uses.mTextures, uses.mTextureRoom);
        }

        /// What the graphics driver held, by region, or null where the system states none.
        std::string asJson(const std::optional<Platform::Process::DriverMemory>& driver)
        {
            if (!driver.has_value())
                return "null";
            return std::format(R"({{"totalVram": {}, "residentVram": {}, "totalGtt": {}, "residentGtt": {}, )"
                               R"("residentCpu": {}}})",
                driver->mTotalVram, driver->mResidentVram, driver->mTotalGtt, driver->mResidentGtt,
                driver->mResidentCpu);
        }

        /// What the mirror's content holds, every count the report's host line reads.
        std::string asJson(const Rtx::ContentMemory& content)
        {
            const Rtx::ReaderMemory& reader = content.mReader;
            std::string out
                = std::format(R"({{"vertexEnd": {}, "verticesUsed": {}, "indexEnd": {}, "indicesUsed": {}, )"
                              R"("restingVertices": {}, "restingIndices": {}, "geometryBytes": {}, )"
                              R"("layerEnd": {}, "maskEnd": {}, "rigRunEnd": {}, "influenceEnd": {}, )"
                              R"("offsetEnd": {}, "poseEnd": {}, "bindVertexEnd": {}, )"
                              R"("restingTextures": {}, "restingTextureBytes": {}, )"
                              R"("aboveWant": {{"stood": {}, "above": {}, "textures": {}, "restingStood": {}, )"
                              R"("restingAbove": {}}}, )"
                              R"("reader": {{"lentModels": {}, "spareModels": {}, "filedModels": {}, )"
                              R"("lentBytes": {}, "spareBytes": {}, "filedBytes": {}, "lentContentBytes": {}, )"
                              R"("filedContentBytes": {}}}, )"
                              R"("prepared": {{"entries": {}, "bytes": {}, "leftPending": {}}}, "ring": [)",
                    content.mVertexEnd, content.mVerticesUsed, content.mIndexEnd, content.mIndicesUsed,
                    content.mRestingVertices, content.mRestingIndices, content.mGeometryBytes, content.mLayerEnd,
                    content.mMaskEnd, content.mRigRunEnd, content.mInfluenceEnd, content.mOffsetEnd, content.mPoseEnd,
                    content.mBindVertexEnd, content.mRestingTextures, content.mRestingTextureBytes,
                    content.mAboveWant.mStood, content.mAboveWant.mAbove, content.mAboveWant.mTextures,
                    content.mAboveWant.mRestingStood, content.mAboveWant.mRestingAbove, reader.mLentModels,
                    reader.mSpareModels, reader.mFiledModels, reader.mLentBytes, reader.mSpareBytes, reader.mFiledBytes,
                    reader.mLentContentBytes, reader.mFiledContentBytes, content.mPrepared.mEntries,
                    content.mPrepared.mBytes, content.mPrepared.mLeftPending);
            for (std::size_t distance = 0; distance < Rtx::sRingDistances; ++distance)
            {
                const Rtx::RingDistance& at = content.mRing.mByDistance[distance];
                out += std::format(R"({}{{"placements": {}, "placementsStanding": {}, "meshes": {}, )"
                                   R"("meshesStanding": {}, "vertices": {}, "verticesStanding": {}}})",
                    distance == 0 ? "" : ", ", at.mPlacements, at.mPlacementsStanding, at.mMeshes, at.mMeshesStanding,
                    at.mVertices, at.mVerticesStanding);
            }
            return out + "]}";
        }

        std::string asJson(const Rtx::TexelReads& reads)
        {
            return std::format(
                R"({{"fact": {}, "factBytes": {}, "file": {}, "fileBytes": {}, "partner": {}, )"
                R"("partnerBytes": {}, "preparation": {}, "preparationBytes": {}, "chainsFoundWhole": {}, )"
                R"("cancelled": {}}})",
                reads.count(Rtx::TexelRead::Fact), reads.bytes(Rtx::TexelRead::Fact), reads.count(Rtx::TexelRead::File),
                reads.bytes(Rtx::TexelRead::File), reads.count(Rtx::TexelRead::Partner),
                reads.bytes(Rtx::TexelRead::Partner), reads.count(Rtx::TexelRead::Preparation),
                reads.bytes(Rtx::TexelRead::Preparation), reads.mChainsFoundWhole, reads.mCancelled);
        }

        std::string asJson(const Arrivals& arrivals)
        {
            std::string worst = "[";
            for (std::size_t at = 0; at < arrivals.mWorstCount; ++at)
                worst += std::format(R"({}{{"frameMs": {}, "arrivedMeshes": {}}})", at == 0 ? "" : ", ",
                    jsonNumber(arrivals.mWorst[at].getFrameMs(), 2), arrivals.mWorst[at].mArrivedMeshes);
            worst += ']';

            return std::format(R"({{"frames": {}, "meshes": {}, "worstMs": {}, "meanMs": {}, "worst": {}}})",
                arrivals.mFrames, arrivals.mMeshes, jsonNumber(arrivals.mWorstMs, 2),
                jsonNumber(arrivals.getMeanMs(), 2), worst);
        }

        /// One of the worst frames, as the place's report prints it: the whole frame, what it
        /// brought, and the three largest spends in it — enough to say whose the frame was.
        std::string describeWorst(const WorstFrame& frame)
        {
            return std::format("{:.1f} ms ({} meshes: {})", frame.getFrameMs(), frame.mArrivedMeshes,
                Rtx::describeStretches(frame.mSpend));
        }

        std::string asJson(const Crossings& crossings)
        {
            return std::format(R"({{"count": {}, "rebuilds": {}, "worstMs": {}, "totalMs": {}}})", crossings.mCount,
                crossings.mRebuilds, jsonNumber(crossings.mWorstMs, 2), jsonNumber(crossings.mTotalMs, 2));
        }

        std::string asJson(const FrameTimes& times)
        {
            return std::format(R"({{"median": {}, "mean": {}, "p95": {}, "p99": {}, "best": {}, "worst": {}}})",
                jsonNumber(times.mMedian, 4), jsonNumber(times.mMean, 4), jsonNumber(times.mP95, 4),
                jsonNumber(times.mP99, 4), jsonNumber(times.mBest, 4), jsonNumber(times.mWorst, 4));
        }

        /// The counts as well as the times, because a zone's distribution is over the frames that ran it: without
        /// them a record cannot tell a tenth of a millisecond every frame from seven every sixtieth.
        std::string asJson(const GpuZone& zone)
        {
            return std::format(R"({{"shareMs": {}, "frames": {}, "ofFrames": {}, "times": {}}})",
                jsonNumber(zone.mShareMs, 4), zone.mFrames, zone.mOfFrames, asJson(zone.mTimes));
        }
    }

    void Arrivals::add(const std::uint32_t arrivedMeshes, const Rtx::FrameSpend& spend)
    {
        const double frameMs = spend.at(Rtx::Timing::Frame);
        if (arrivedMeshes > 0)
        {
            ++mFrames;
            mMeshes += arrivedMeshes;
            mSumMs += frameMs;
            mWorstMs = std::max(mWorstMs, frameMs);
        }

        // Kept longest first, so the shortest kept is the one a longer frame pushes out.
        std::size_t at = mWorstCount;
        while (at > 0 && mWorst[at - 1].getFrameMs() < frameMs)
            --at;
        if (at >= sKept)
            return;

        for (std::size_t behind = std::min(mWorstCount, sKept - 1); behind > at; --behind)
            mWorst[behind] = mWorst[behind - 1];
        mWorst[at] = WorstFrame{ .mArrivedMeshes = arrivedMeshes, .mSpend = spend };
        mWorstCount = std::min(mWorstCount + 1, sKept);
    }

    std::int32_t minuteOfDay(const float hour)
    {
        // In double, where a float times sixty is exact: rounded in float first, an hour a hair
        // under a half minute would round up to it and then to the minute after.
        return static_cast<std::int32_t>(std::lround(static_cast<double>(hour) * 60.0)) % (24 * 60);
    }

    std::string describeHour(const float hour)
    {
        const std::int32_t minutes = minuteOfDay(hour);
        return std::format("{:02}:{:02}", minutes / 60, minutes % 60);
    }

    namespace
    {
        std::string describeExposure(const Rtx::ExposureRule& exposure)
        {
            if (const Rtx::FixedExposure* fixed = std::get_if<Rtx::FixedExposure>(&exposure))
                return std::format("fixed at {:.3f}", fixed->mScale);
            return std::holds_alternative<Rtx::HeldExposure>(exposure) ? "held" : "adapted";
        }

        std::string describeStep(const std::optional<float>& step)
        {
            return step.has_value() ? std::format("{:.4f} s", *step) : std::string("the wall");
        }

        /// Whose patterns named the companion maps: the player's in a played session, the shipped
        /// file's in every other.
        std::string_view describeMaps(const bool played)
        {
            return played ? "player" : "shipped";
        }

        std::string_view describeSettled(const std::optional<bool>& settled)
        {
            if (!settled.has_value())
                return "as the step says";
            return *settled ? "settled" : "streamed";
        }

        std::string describeBudget(const std::optional<std::uint64_t>& budget)
        {
            return budget.has_value() ? std::format("{:.1f} MiB", Rtx::mebibytes(*budget)) : std::string("none");
        }

        std::string describeHugePages(const std::optional<float>& share)
        {
            return share.has_value() ? std::format("{:.0f}% huge", *share * 100.0f) : std::string("not said");
        }
    }

    std::string describeNotFinite(const Rtx::NotFinite& wrote)
    {
        std::string text = std::format("{} stores not finite", wrote.total());
        std::string_view separator = ": ";
        for (const Rtx::NotFiniteStores& kernel : wrote.kernels())
        {
            text += std::format("{}{} in {}", separator, kernel.mStores, kernel.mKernel);
            if (kernel.mFirst.has_value())
                text += std::format(" (first at {}, {})", kernel.mFirst->mX, kernel.mFirst->mY);
            separator = ", ";
        }
        return text;
    }

    std::string describeNotFiniteFrame(const Rtx::NotFiniteFrame& kept)
    {
        std::string text = std::format("frame {}, {} by {}", kept.mFrame, kept.mWidth, kept.mHeight);
        text += kept.mCentre.has_value() ? std::format(", kept around {}, {}\n", kept.mCentre->mX, kept.mCentre->mY)
                                         : std::string(": no image held a value not finite, so no window was kept\n");

        for (const Rtx::NotFiniteImage& image : kept.mImages)
        {
            text += image.mName;
            if (image.mWords)
                text += ": words, not searched\n";
            else if (image.mFirst.has_value())
                text += std::format(": first not finite at {}, {}\n", image.mFirst->mX, image.mFirst->mY);
            else
                text += ": none not finite\n";

            if (!kept.mCentre.has_value())
                continue;

            constexpr auto sRadius = static_cast<std::int64_t>(Rtx::NotFiniteImage::sRadius);
            const std::int64_t left = std::int64_t{ kept.mCentre->mX } - sRadius;
            const std::int64_t top = std::int64_t{ kept.mCentre->mY } - sRadius;
            text += std::format("  from x {}:\n", left);
            for (std::uint32_t row = 0; row < Rtx::NotFiniteImage::sSide; ++row)
            {
                const std::int64_t y = top + row;
                text += std::format("  y {}:", y);
                std::string_view separator = " ";
                for (std::uint32_t column = 0; column < Rtx::NotFiniteImage::sSide; ++column)
                {
                    const std::int64_t x = left + column;
                    text += separator;
                    separator = " | ";
                    if (x < 0 || y < 0 || std::cmp_greater_equal(x, kept.mWidth)
                        || std::cmp_greater_equal(y, kept.mHeight))
                    {
                        text += '-';
                        continue;
                    }

                    const std::array<std::uint32_t, 4>& texel
                        = image.mTexels[row * Rtx::NotFiniteImage::sSide + column];
                    if (image.mWords)
                        text += std::format(
                            "{:#010x} {:#010x} {:#010x} {:#010x}", texel[0], texel[1], texel[2], texel[3]);
                    else
                        text += std::format("{} {} {} {}", std::bit_cast<float>(texel[0]),
                            std::bit_cast<float>(texel[1]), std::bit_cast<float>(texel[2]),
                            std::bit_cast<float>(texel[3]));
                }
                text += '\n';
            }
        }
        return text;
    }

    std::string describeHostHeld(const Rtx::ContentMemory& content)
    {
        // The host's side of the device's question: runs a freed mesh left as holes, which nothing moves to close,
        // and what the cell reader keeps of models it no longer lends.
        const Rtx::ReaderMemory& reader = content.mReader;
        if (content.mVertexEnd == 0 && reader.mLentModels + reader.mSpareModels + reader.mFiledModels == 0)
            return {};

        return std::format(
            "  host  vertices {:.2f} M placed, {:.2f} M resting, {:.2f} M in holes of {:.2f} M reached   indices "
            "{:.2f} M, {:.2f} M, {:.2f} M of {:.2f} M   geometry {:.1f} MiB   reader {:.1f} MiB lent in {} models "
            "holding {:.1f}, {:.1f} MiB spare in {}, {:.1f} MiB filed unlent in {} holding {:.1f}\n"
            "  runs  layers {}   masks {}   rig runs {}   influences {}   offsets {}   pose words {}   "
            "bind vertices {}   resting textures {}, {:.1f} MiB   above want {:.1f} of {:.1f} MiB stood whole, in {} "
            "textures, {:.1f} of {:.1f} MiB resting   prepared {} unhanded, {:.1f} MiB, {} slots left pending\n",
            (content.mVerticesUsed - content.mRestingVertices) / 1e6, content.mRestingVertices / 1e6,
            (content.mVertexEnd - content.mVerticesUsed) / 1e6, content.mVertexEnd / 1e6,
            (content.mIndicesUsed - content.mRestingIndices) / 1e6, content.mRestingIndices / 1e6,
            (content.mIndexEnd - content.mIndicesUsed) / 1e6, content.mIndexEnd / 1e6,
            Rtx::mebibytes(content.mGeometryBytes), Rtx::mebibytes(reader.mLentBytes), reader.mLentModels,
            Rtx::mebibytes(reader.mLentContentBytes), Rtx::mebibytes(reader.mSpareBytes), reader.mSpareModels,
            Rtx::mebibytes(reader.mFiledBytes), reader.mFiledModels, Rtx::mebibytes(reader.mFiledContentBytes),
            content.mLayerEnd, content.mMaskEnd, content.mRigRunEnd, content.mInfluenceEnd, content.mOffsetEnd,
            content.mPoseEnd, content.mBindVertexEnd, content.mRestingTextures,
            Rtx::mebibytes(content.mRestingTextureBytes), Rtx::mebibytes(content.mAboveWant.mAbove),
            Rtx::mebibytes(content.mAboveWant.mStood), content.mAboveWant.mTextures,
            Rtx::mebibytes(content.mAboveWant.mRestingAbove), Rtx::mebibytes(content.mAboveWant.mRestingStood),
            content.mPrepared.mEntries, Rtx::mebibytes(content.mPrepared.mBytes), content.mPrepared.mLeftPending);
    }

    std::string describeGameHeld(const GameHeld& game)
    {
        if (game.mLoadedCells + game.mPreloadedCells == 0)
            return {};
        return std::format(
            "  game  cell stores {} loaded, {} preloaded   caches {} looks for room finding all in use\n",
            game.mLoadedCells, game.mPreloadedCells, game.mStarvedLooks);
    }

    std::string describeRingStanding(const Rtx::RingStanding& ring)
    {
        // Each distance the ring holds anything at, what stands of it beside what it holds.
        std::string out;
        for (std::size_t distance = 0; distance < Rtx::sRingDistances; ++distance)
        {
            const Rtx::RingDistance& at = ring.mByDistance[distance];
            if (at.mPlacements == 0)
                continue;
            out += std::format("   {}{}: placements {} of {}, meshes {} of {}, {:.2f} of {:.2f} M vertices", distance,
                distance + 1 == Rtx::sRingDistances ? " and past" : "", at.mPlacementsStanding, at.mPlacements,
                at.mMeshesStanding, at.mMeshes, static_cast<double>(at.mVerticesStanding) / 1e6,
                static_cast<double>(at.mVertices) / 1e6);
        }
        return out.empty() ? std::string() : "  ring  standing of held by distance" + out + "\n";
    }

    std::string describeTexelReads(const Rtx::TexelReads& reads)
    {
        // Said only where a header was asked at all: a renderer that reads no file through one asks nothing.
        std::uint64_t asked = 0;
        for (const std::uint32_t count : reads.mCounts)
            asked += count;
        if (asked == 0)
            return {};

        std::string out = "  texels asked";
        for (const auto& [why, name] : { std::pair{ Rtx::TexelRead::Fact, "fact" },
                 std::pair{ Rtx::TexelRead::File, "file" }, std::pair{ Rtx::TexelRead::Partner, "partner" },
                 std::pair{ Rtx::TexelRead::Preparation, "preparation" } })
            out += std::format("   {} {} ({:.1f} MiB)", name, reads.count(why), Rtx::mebibytes(reads.bytes(why)));
        out += std::format("   chains found whole {}   cancelled {}\n", reads.mChainsFoundWhole, reads.mCancelled);
        return out;
    }

    std::string describeHeader(const BenchHeader& header)
    {
        // The build and the layers first, because either makes every figure below one not to quote, and the
        // command's own word on whether it measures beside them.
        const Rtx::RenderProfile& profile = header.mSetup.mRun.mProfile;
        const MWRender::MirrorKnobs& mirror = header.mSetup.mMirror;
        std::string out = std::format("\nrun  {}, layers {}, {}{}{}\n",
            header.mAsserts ? "a build with asserts, not one to quote" : "a release build",
            header.mValidating ? "on, not a figure to quote" : "off", header.mMeasures ? "measured" : "not measured",
            header.mHashed ? ", every frame hashed" : "", header.mTurnsWeather ? ", the weather turned" : "");
        const Rtx::Reconstruction& resolved = header.mReconstruction;
        out += std::format(
            "     {}x{} from {}x{}, upscale {}, filter {}, jitter {}, noise {}, level bias {:.3f}, "
            "antilag {}, history fix {}, dual motion {}, anti-firefly {}, shadow floor {:.4f}, lamp "
            "candidates {}, indirect {}, bounce rays {}, relief {}, soft edge dither {}, water interleave {}\n",
            header.mExtents.mOutputWidth, header.mExtents.mOutputHeight, header.mExtents.mRenderWidth,
            header.mExtents.mRenderHeight, Rtx::sUpscaleNames.name(resolved.mUpscale),
            Rtx::sLightFilterNames.name(resolved.mFilter), resolved.mJitter ? "on" : "off",
            Rtx::sNoiseSourceNames.name(resolved.mNoise), resolved.mLevelBias,
            resolved.mFilters.mAntilag ? "on" : "off", resolved.mFilters.mHistoryFix ? "on" : "off",
            resolved.mFilters.mDualMotion ? "on" : "off", resolved.mFilters.mAntiFirefly ? "on" : "off",
            resolved.mSampling.mShadowFloor, resolved.mSampling.mLampCandidates,
            resolved.mSampling.mIndirect ? "on" : "off", resolved.mSampling.mBounceRays,
            resolved.mSampling.mRelief ? "on" : "off", resolved.mSoftEdgeDither ? "on" : "off",
            resolved.mSampling.mWaterInterleave ? "on" : "off");
        out += std::format("     gamma {:.2f}, show {}, exposure {}, variants {}, hold {}\n", profile.mGamma,
            Rtx::sSurfaceViewNames.name(profile.mShow), describeExposure(profile.mExposure),
            profile.mSpecializeKernels ? "on" : "off",
            profile.mStressOverlapMs > 0.0 ? std::format("{:.1f} ms", profile.mStressOverlapMs) : std::string("none"));
        out += std::format(
            "     land {:.1f} cells, viewing distance {:.0f}, distant statics {}, maps {}, step {}, walks {}, "
            "memory budget {}, cache budget {}, host pages {}\n",
            mirror.mReach.mCells, mirror.mReach.mViewingDistance, mirror.mDistantStatics ? "on" : "off",
            describeMaps(header.mPlayed), describeStep(header.mStep), describeSettled(header.mSetup.mSettled),
            describeBudget(header.mSetup.mRun.mMemoryBudget), describeBudget(header.mSetup.mCacheBudget),
            describeHugePages(header.mHugePageShare));
        return out;
    }

    std::string describePlace(const BenchPlace& place, const bool measured)
    {
        std::string out;

        // Each line only where there is something to put in it: a game measuring itself names no view, cell or
        // built scene, and a row of empty quotes and zeroes reads as a measurement of nothing.
        if (!place.mView.empty())
        {
            out += '\n' + place.mView;
            if (!place.mNote.empty())
                out += " — " + place.mNote;

            out += '\n';
        }

        if (!place.mCell.empty())
            out += std::format("  cell {} at {} in {}   {}\n", place.mCell, describeHour(place.mHour), place.mWeather,
                Rtx::describeSceneHeld(place.mScene));

        // Under the scene's line, because it is the same fact from the device's side: the line above says what the
        // content came to, this what the card gave up for it, which a card with a small host-visible heap runs out
        // of first.
        out += Rtx::describeMemory(place.mMemory);
        if (place.mDriver.has_value())
            out += std::format(
                "  the driver: vram {:.1f} of {:.1f} MiB standing, gtt {:.1f} of {:.1f}, moved out of the device's "
                "reach {:.1f}\n",
                Rtx::mebibytes(place.mDriver->mResidentVram), Rtx::mebibytes(place.mDriver->mTotalVram),
                Rtx::mebibytes(place.mDriver->mResidentGtt), Rtx::mebibytes(place.mDriver->mTotalGtt),
                Rtx::mebibytes(place.mDriver->mResidentCpu));

        out += describeHostHeld(place.mContent);
        out += describeRingStanding(place.mContent.mRing);
        out += describeGameHeld(place.mGame);
        out += describeTexelReads(place.mTexelReads);

        // How much of the frame hit something is what tells "the cell rendered" from "the camera faced away".
        if (place.mHitPercent > 0.0)
            out += std::format("  {:.1f}% of primary rays hit\n", place.mHitPercent);

        if (place.mOverlap.mFrames > 0)
            out += std::format("  {:.2f} frames in flight at a submit, {} at the least\n", place.mOverlap.getMean(),
                place.mOverlap.mLeast);

        if (!measured)
            out += "  not a measurement: this command draws its frames to look at them, and times them as it goes\n";
        out += describeTimings(place.mRows);

        // The device's own account of the same frame, one figure each: six distributions would be a wall, and this
        // row answers which of them is the expensive one. Each figure is the zone's share of the average frame, so
        // a pass that only runs at a crossing says so beside its own share.
        out += describeZones(place.mGpu);
        out += describeCounters(place.mGpu, place.mCounterNames);
        out += describeClock(place.mClock);

        // Under the clock, because it is the other premise every figure above rests on: a place
        // another process drew through is the desktop's reading and not the renderer's.
        out += "  " + describeCard(place.mCard) + '\n';
        out += "  " + describeCounts(place.mThread) + '\n';

        // Only for a route. The worst is the one to read: a crossing is a dropped frame, and an average over six
        // hundred frames of which four were expensive hides exactly the thing.
        if (place.mCrossings.mCount > 0)
            out += std::format("  {} crossings, {} of them rebuilds — {:.0f} ms worst, {:.1f} s over the run{}\n",
                place.mCrossings.mCount, place.mCrossings.mRebuilds, place.mCrossings.mWorstMs,
                place.mCrossings.mTotalMs / 1000.0,
                place.mTravelled < 1.0 ? std::format(", {:.0f}% of the route flown", place.mTravelled * 100.0) : "");

        // The arrivals beside the crossings, at every place: a crossing is a route's, but a mesh arrives wherever an
        // actor walks in, and what the worst frames carried says whether the tail is theirs.
        if (place.mArrivals.mFrames > 0)
            out += std::format(
                "  {} frames extended the scene with {} meshes — {:.1f} ms worst, {:.1f} mean against "
                "the {:.1f} median\n",
                place.mArrivals.mFrames, place.mArrivals.mMeshes, place.mArrivals.mWorstMs, place.mArrivals.getMeanMs(),
                place.at(Rtx::Timing::Frame).mMedian);
        if (place.mArrivals.mWorstCount > 0)
        {
            out += "  worst frames:";
            for (std::size_t at = 0; at < place.mArrivals.mWorstCount; ++at)
                out += std::format("{} {}", at == 0 ? "" : ",", describeWorst(place.mArrivals.mWorst[at]));
            out += '\n';
        }

        out += std::format("  {} frames in {:.2f} s — {:.1f} fps, {:.1f} at the 1% low\n", place.mFrames,
            place.mWallSeconds, place.at(Rtx::Timing::Frame).getRate(), place.at(Rtx::Timing::Frame).getLowRate());

        return out;
    }

    std::string describeTotal(std::span<const BenchPlace> places)
    {
        if (places.size() < 2)
            return {};

        std::uint32_t frames = 0;
        double lasted = 0.0;
        for (const BenchPlace& place : places)
        {
            frames += place.mFrames;
            lasted += place.mWallSeconds;
        }

        return std::format("\n{} places, {} frames in {:.1f} s\n", places.size(), frames, lasted);
    }

    Result<void, std::string> writeJson(
        const std::filesystem::path& path, const BenchHeader& header, const std::span<const BenchPlace> places)
    {
        std::ostringstream file;

        file
            << "{\n"
            << std::format(R"(  "suite": {},)", asJson(header.mSuite)) << '\n'
            << std::format(R"(  "output": [{}, {}],)", header.mExtents.mOutputWidth, header.mExtents.mOutputHeight)
            << '\n'
            << std::format(R"(  "render": [{}, {}],)", header.mExtents.mRenderWidth, header.mExtents.mRenderHeight)
            << '\n'
            << std::format(R"(  "upscale": "{}", "lightFilter": "{}", "jittered": {},)",
                   Rtx::sUpscaleNames.name(header.mReconstruction.mUpscale),
                   Rtx::sLightFilterNames.name(header.mReconstruction.mFilter), header.mReconstruction.mJitter)
            << '\n'
            << std::format(
                   R"(  "noise": "{}", "levelBias": {}, "antilag": {}, )"
                   R"("historyFix": {}, "dualMotion": {}, "antiFirefly": {}, "shadowFloor": {}, "lampCandidates": {}, )"
                   R"("indirect": {}, "bounceRays": {}, "relief": {}, "softEdgeDither": {}, "waterInterleave": {},)",
                   Rtx::sNoiseSourceNames.name(header.mReconstruction.mNoise),
                   jsonNumber(header.mReconstruction.mLevelBias, 3), header.mReconstruction.mFilters.mAntilag,
                   header.mReconstruction.mFilters.mHistoryFix, header.mReconstruction.mFilters.mDualMotion,
                   header.mReconstruction.mFilters.mAntiFirefly,
                   jsonNumber(header.mReconstruction.mSampling.mShadowFloor, 4),
                   header.mReconstruction.mSampling.mLampCandidates, header.mReconstruction.mSampling.mIndirect,
                   header.mReconstruction.mSampling.mBounceRays, header.mReconstruction.mSampling.mRelief,
                   header.mReconstruction.mSoftEdgeDither, header.mReconstruction.mSampling.mWaterInterleave)
            << '\n'
            << std::format(R"(  "frames": {}, "warmup": {}, "validation": {},)", header.mMeasured, header.mWarmup,
                   header.mValidating)
            << '\n'
            << std::format(R"(  "asserts": {}, "measures": {}, "hashed": {}, "turnsWeather": {}, "maps": "{}", )"
                           R"("hugePageShare": {},)",
                   header.mAsserts, header.mMeasures, header.mHashed, header.mTurnsWeather,
                   describeMaps(header.mPlayed), asJson(header.mHugePageShare))
            << '\n'
            << asJson(header.mSetup, header.mStep) << '\n'
            << R"(  "places": [)" << '\n';

        for (std::size_t at = 0; at < places.size(); ++at)
        {
            const BenchPlace& place = places[at];
            file << std::format(R"(    {{"view": {}, "cell": {}, "hour": {}, "weather": {}, )", asJson(place.mView),
                asJson(place.mCell), jsonNumber(place.mHour, 4), asJson(place.mWeather))
                 << R"("scene": )" << asJson(place.mScene) << R"(, "memory": )" << asJson(place.mMemory)
                 << R"(, "driver": )" << asJson(place.mDriver) << R"(, "content": )" << asJson(place.mContent)
                 << R"(, "texelReads": )" << asJson(place.mTexelReads)
                 << std::format(R"(, "game": {{"loadedCells": {}, "preloadedCells": {}, "starvedLooks": {}}})",
                        place.mGame.mLoadedCells, place.mGame.mPreloadedCells, place.mGame.mStarvedLooks)
                 << std::format(R"(, "frames": {}, "wallSeconds": {}, "hitPercent": {}, )", place.mFrames,
                        jsonNumber(place.mWallSeconds, 4), jsonNumber(place.mHitPercent, 2))
                 << R"("crossings": )" << asJson(place.mCrossings) << R"(, "arrivals": )" << asJson(place.mArrivals)
                 << std::format(R"(, "overlap": {{"mean": {}, "least": {}}}, "travelled": {}, )",
                        jsonNumber(place.mOverlap.getMean(), 4), place.mOverlap.mLeast,
                        jsonNumber(place.mTravelled, 4));

            for (const Rtx::Timing timing : Rtx::sTimings.values())
                file << std::format(R"("{}Ms": )", Rtx::sTimings.name(timing)) << asJson(place.mRows[indexOf(timing)])
                     << ", ";

            file << R"("gpuMs": {)";

            for (std::size_t zone = 0; zone < place.mGpu.size(); ++zone)
                file << std::format(R"({}{}: {})", zone == 0 ? "" : ", ",
                    asJson(Rtx::sFrameZoneNames.name(place.mGpu[zone].mZone)), asJson(place.mGpu[zone]));

            // Beside the times and not inside them, so a record read for its times reads as it did.
            file << R"(}, "gpuCounters": {)";
            for (std::size_t zone = 0; zone < place.mGpu.size() && !place.mCounterNames.empty(); ++zone)
            {
                file << std::format(
                    R"({}{}: {{)", zone == 0 ? "" : ", ", asJson(Rtx::sFrameZoneNames.name(place.mGpu[zone].mZone)));
                for (std::size_t counter = 0; counter < place.mCounterNames.size(); ++counter)
                {
                    // A counter no run read a number for is null, which JSON has and NaN it has not.
                    file << std::format(R"({}{}: {})", counter == 0 ? "" : ", ", asJson(place.mCounterNames[counter]),
                        jsonSignificant(place.mGpu[zone].mCounters[counter], 6));
                }
                file << '}';
            }

            file << "}, \"clock\": " << asJson(place.mClock) << ", \"card\": " << asJson(place.mCard)
                 << ", \"thread\": " << asJson(place.mThread) << "}" << (at + 1 < places.size() ? "," : "") << '\n';
        }

        file << "  ]\n}\n";

        // A record compared across commits that was never written would leave the last one at its path to be
        // compared as this one, so a failed write is reported.
        return writeReport(path, file.view());
    }
}
