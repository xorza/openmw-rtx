#include "runrecord.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <components/files/conversion.hpp>
#include <components/platform/folder.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/common/asserts.hpp>
#include <components/rtx/common/mebibytes.hpp>
#include <components/rtx/renderer/memoryreport.hpp>

#include "heapdescription.hpp"

namespace RtxTool
{
    namespace
    {
        /// What the device's video heap held by use, the frame's own memory among it.
        std::uint64_t heldByUse(const Rtx::MemoryUses& uses)
        {
            return uses.mFrame + uses.mEssential + uses.mStructures + uses.mTextures;
        }

        /// The meshes the ring holds that stand in no placement, and their vertices, at every distance together.
        struct Unstood
        {
            std::uint64_t mMeshes = 0;
            std::uint64_t mVertices = 0;
        };

        Unstood unstoodOf(const Rtx::RingStanding& ring)
        {
            Unstood unstood;
            for (const Rtx::RingDistance& at : ring.mByDistance)
            {
                unstood.mMeshes += at.mMeshes - at.mMeshesStanding;
                unstood.mVertices += at.mVertices - at.mVerticesStanding;
            }
            return unstood;
        }

        std::uint64_t readerRoomOf(const Rtx::ReaderMemory& reader)
        {
            return reader.mLentBytes + reader.mSpareBytes + reader.mFiledBytes;
        }

        /// The place whose end found `key` at its most, or null where none found any: what a run's peak of a figure
        /// each stop's end measured names.
        template <class Key>
        const BenchPlace* peakOf(const std::vector<BenchPlace>& places, Key key)
        {
            const auto peak = std::max_element(places.begin(), places.end(),
                [&](const BenchPlace& a, const BenchPlace& b) { return key(a) < key(b); });
            return peak == places.end() || key(*peak) == 0 ? nullptr : &*peak;
        }

        /// A run's peak of each figure a stop's end measured, a line each, with the stop that found it; the counts
        /// that only grow are the last stop's. A figure no stop found is left out, as a renderer that does not say it.
        std::string describePeaks(const std::vector<BenchPlace>& places)
        {
            std::string out;
            if (const BenchPlace* const peak
                = peakOf(places, [](const BenchPlace& place) { return heldByUse(place.mMemory.mUses); }))
                out += std::format("the device held at most {:.1f} MiB by use, at {}: {}\n",
                    Rtx::mebibytes(heldByUse(peak->mMemory.mUses)), peak->mView,
                    Rtx::describeUses(peak->mMemory.mUses));

            if (const BenchPlace* const peak = peakOf(places, [](const BenchPlace& place) {
                    return place.mContent.mGeometryBytes + place.mScene.mGeometryBytes;
                }))
            {
                const Rtx::ContentMemory& content = peak->mContent;
                out += std::format(
                    "the geometry reached at most {:.2f} M vertices and {:.2f} M indices, at {}: {:.2f} M and {:.2f} M "
                    "of them resting, {:.2f} M and {:.2f} M in holes; {:.1f} MiB on the host, {:.1f} MiB on the "
                    "device\n",
                    content.mVertexEnd / 1e6, content.mIndexEnd / 1e6, peak->mView, content.mRestingVertices / 1e6,
                    content.mRestingIndices / 1e6, (content.mVertexEnd - content.mVerticesUsed) / 1e6,
                    (content.mIndexEnd - content.mIndicesUsed) / 1e6, Rtx::mebibytes(content.mGeometryBytes),
                    Rtx::mebibytes(peak->mScene.mGeometryBytes));
            }

            if (const BenchPlace* const peak
                = peakOf(places, [](const BenchPlace& place) { return unstoodOf(place.mContent.mRing).mMeshes; }))
            {
                const Unstood unstood = unstoodOf(peak->mContent.mRing);
                out += std::format(
                    "the ring held at most {} meshes standing in no placement, {:.2f} M vertices, at {}\n",
                    unstood.mMeshes, static_cast<double>(unstood.mVertices) / 1e6, peak->mView);
            }

            if (const BenchPlace* const peak
                = peakOf(places, [](const BenchPlace& place) { return readerRoomOf(place.mContent.mReader); }))
            {
                const Rtx::ReaderMemory& reader = peak->mContent.mReader;
                out += std::format("the reader held at most {:.1f} MiB of room for {:.1f} MiB of models, at {}\n",
                    Rtx::mebibytes(readerRoomOf(reader)),
                    Rtx::mebibytes(reader.mLentContentBytes + reader.mFiledContentBytes), peak->mView);
            }

            if (const BenchPlace* const peak
                = peakOf(places, [](const BenchPlace& place) { return place.mGame.mLoadedCells; }))
                out += std::format(
                    "the game held at most {} cell stores loaded, at {}\n", peak->mGame.mLoadedCells, peak->mView);

            // What only grows, at the run's end.
            if (!places.empty())
            {
                const BenchPlace& last = places.back();
                if (last.mGame.mStarvedLooks > 0)
                    out += std::format("{} looks for room in the caches found all in use\n", last.mGame.mStarvedLooks);
                if (const std::string reads = describeTexelReads(last.mTexelReads); !reads.empty())
                    out += "over the run" + reads.substr(1);
            }
            return out.empty() ? out : "\n" + out;
        }
    }

    void RunRecord::begin(const SessionRequest& request)
    {
        mHeader.mSuite = request.mSuite;
        mHeader.mAsserts = Rtx::sAssertsOn;
        mHeader.mMeasures = request.mMeasures;
        mHeader.mPlayed = request.mPlayed;
        mKeepsPictures = request.mKeepsPictures;
        mChecksAreFindings = request.mWatching.has_value();
        mHeader.mHashed = std::any_of(
            request.mStops.begin(), request.mStops.end(), [](const Stop& stop) { return stop.mActions.mHash; });
        mHeader.mTurnsWeather = std::any_of(request.mStops.begin(), request.mStops.end(),
            [](const Stop& stop) { return !stop.mSky.mTurnThrough.empty(); });
        mHeader.mSetup = request.mSetup;
        mHeader.mStep = request.mStep;

        // Everything an earlier run wrote where this one writes goes before this one draws, as `noise` clears its
        // own: a run that dies before `finish` would leave the last run's hashes beside its own pictures for a later
        // `--against` to compare, and a stop this run never reached would read as the last run's picture. Every
        // output the request names: the record, each stop's files, and the measured frames' pictures.
        std::error_code ignored;
        const auto removeEarlier = [&](const std::filesystem::path& earlier) {
            if (!earlier.empty())
                std::filesystem::remove(earlier, ignored);
        };
        removeEarlier(request.mHashes);
        removeEarlier(request.mJson);
        for (const Stop& stop : request.mStops)
        {
            const Actions& actions = stop.mActions;
            for (const std::filesystem::path& file : { actions.mFrameTimes, actions.mMapTile })
                removeEarlier(file);
            if (actions.mCapture.has_value())
                removeEarlier(actions.mCapture->mFile);
            if (actions.mMean.has_value())
                removeEarlier(actions.mMean->mFile);
            if (actions.mDoll.has_value())
                removeEarlier(actions.mDoll->mFile);
        }

        if (request.mPictures.empty() || !std::filesystem::is_directory(request.mPictures, ignored))
            return;
        std::vector<std::filesystem::path> pictured;
        for (const std::filesystem::directory_entry& entry :
            Platform::listFolder(request.mPictures).value_or(std::vector<std::filesystem::directory_entry>()))
        {
            const std::string name = Files::pathToUnicodeString(entry.path().filename());
            const bool ofAStop = std::any_of(request.mStops.begin(), request.mStops.end(), [&](const Stop& stop) {
                constexpr std::string_view extension = ".png";
                const std::size_t from = stop.mName.size() + 1;
                if (name.size() <= from + extension.size() || !name.starts_with(stop.mName) || name[from - 1] != '-'
                    || !name.ends_with(extension))
                    return false;
                const std::string_view frame
                    = std::string_view(name).substr(from, name.size() - from - extension.size());
                return std::all_of(frame.begin(), frame.end(), [](char c) { return c >= '0' && c <= '9'; });
            });
            if (entry.is_regular_file(ignored) && ofAStop)
                pictured.push_back(entry.path());
        }
        for (const std::filesystem::path& path : pictured)
            removeEarlier(path);
    }

    void RunRecord::add(BenchPlace place)
    {
        // Read once the first place has loaded, which is the memory the frames walk.
        if (mPlaces.empty())
        {
            mHeader.mHugePageShare = Platform::Process::hugePageShare();
            mCompactionStalls = Platform::Process::compactionStalls();
            mReport += describeHeader(mHeader);
        }
        mPlaces.push_back(std::move(place));
        mReport += describePlace(mPlaces.back(), mHeader.mMeasures);
    }

    void RunRecord::noteHostMemory(const std::string_view stop, const Platform::Process::AnonymousMemory& anonymous,
        std::optional<Platform::HeapHeld> heap)
    {
        const auto held
            = [](const Platform::Process::AnonymousMemory& memory) { return memory.mResident + memory.mNotResident; };
        if (mHostPeak.has_value() && held(anonymous) <= held(mHostPeak->mAnonymous))
            return;
        mHostPeak = HostPeak{ .mStop = std::string(stop), .mAnonymous = anonymous, .mHeap = std::move(heap) };
    }

    void RunRecord::checked(const Check check, const bool held)
    {
        ++mChecked;
        if (held)
            return;
        ++mFailed;
        mFailedChecks.push_back(check);
        if (!mChecksAreFindings)
            fail();
    }

    void RunRecord::skip(const Stop& stop, const std::string_view why)
    {
        mReport += std::format("\n{}: passed over, {}{}\n", stop.mName.empty() ? "unnamed" : stop.mName, why,
            stop.mWhenAbsent == WhenAbsent::Fails ? "" : ", which a run of every view does not fail on");
        if (stop.mWhenAbsent == WhenAbsent::Fails)
            fail();
    }

    void RunRecord::fail(const Stop& stop, const std::string_view why)
    {
        mReport += std::format("\n{}: failed, {}\n", stop.mName.empty() ? "unnamed" : stop.mName, why);
        fail();
    }

    SessionResult RunRecord::conclude(const Stop* const left)
    {
        SessionResult result;
        result.mExitStatus = mStatus;
        result.mShelf = std::move(mShelf);
        result.mTraced = mTraced;
        result.mPlaces = mPlaces;
        result.mReport = mReport;
        if (left != nullptr)
            result.mLeft = *left;

        return result;
    }

    void RunRecord::finish(const SessionRequest& request)
    {
        mReport += describeTotal(mPlaces);

        // A run the kernel compacted memory under, said: on huge pages a compaction stalls the allocation that
        // asked, 50–60 ms inside a crossing, which is the machine's and not the renderer's. Kept on huge pages,
        // whose placement steadies every other figure.
        const std::optional<std::uint64_t> stalls = Platform::Process::compactionStalls();
        if (mCompactionStalls.has_value() && stalls.has_value() && *stalls > *mCompactionStalls)
            mReport += std::format(
                "\nthe kernel compacted memory {} times during the run, each a stalled allocation: "
                "a worst frame may be one\n",
                *stalls - *mCompactionStalls);

        // **What the process held of the system's memory at its peak**: on a machine short of memory what it holds
        // the system takes from everything else, and the heap's free share says whether it is data or what the
        // allocator kept of what was freed.
        if (mHostPeak.has_value())
        {
            const HostPeak& peak = *mHostPeak;
            mReport += std::format(
                "\nthe process held at most {:.1f} MiB of anonymous memory, {:.1f} MiB of it not resident, "
                "at {}",
                Rtx::mebibytes(peak.mAnonymous.mResident + peak.mAnonymous.mNotResident),
                Rtx::mebibytes(peak.mAnonymous.mNotResident), peak.mStop);
            if (peak.mHeap.has_value())
                mReport += "; there " + describeHeap(*peak.mHeap);
            mReport += '\n';
        }

        // **Each figure a stop's end measured, at the run's peak**, with the stop that found it: the device by use,
        // the geometry, the ring, the prepared images, the reader and the game.
        mReport += describePeaks(mPlaces);

        // **What a `check` run came to, in one line.** A suite asks every check at each of several
        // places, so the verdict is otherwise something a reader counts by hand.
        if (mChecked > 0)
            mReport += std::format("\n{} checks asked, {} failed\n", mChecked, mFailed);

        if (!request.mHashes.empty() && wrote(mHashes.write(request.mHashes)))
            mReport += std::format(
                "\nwrote {} frame hashes to {}\n", mHashes.frameCount(), Files::pathToUnicodeString(request.mHashes));

        if (!request.mAgainst.empty())
        {
            mReport += std::format("\nagainst {}\n", Files::pathToUnicodeString(request.mAgainst));
            for (const FrameHashes::ViewDifference& difference : mHashes.against(mReference))
            {
                mReport += std::format("  {:<28} {}\n", difference.mView, describeDifference(difference));
                if (!difference.same())
                    differ();
            }
        }

        // **A record that measured no place is written all the same**: one that was not would leave the last run's
        // at its path, to be compared as this one's.
        save(request);
        bool written = true;
        for (const std::string& failure : mFiles.finish())
            written = wrote(Result<void, std::string>(Err{ failure })) && written;
        if (written && !request.mJson.empty())
            mReport += "wrote " + Files::pathToUnicodeString(request.mJson) + '\n';
    }

    void RunRecord::save(const SessionRequest& request)
    {
        if (request.mJson.empty())
            return;

        if (!mHeaderSaved)
        {
            mFiles.write(request.mJson, recordHeaderLine(mHeader), FileWriter::As::Whole);
            mHeaderSaved = true;
        }
        for (; mPlacesSaved < mPlaces.size(); ++mPlacesSaved)
            mFiles.write(request.mJson, recordPlaceLine(mPlaces[mPlacesSaved]), FileWriter::As::Appended);
        if (mFindingCounts.has_value() && mFindingCounts != mCountsSaved)
        {
            mFiles.write(request.mJson, recordFindingsLine(*mFindingCounts), FileWriter::As::Appended);
            mCountsSaved = mFindingCounts;
        }
    }

    void RunRecord::writeBeside(const std::filesystem::path& path, std::string text)
    {
        mFiles.write(path, std::move(text), FileWriter::As::Whole);
    }

    void RunRecord::abandon(const SessionRequest& request)
    {
        fail();
        mHashes.dropUnpictured();
        finish(request);
    }

    bool RunRecord::wrote(const Result<void, std::string>& written)
    {
        if (written.isOk())
            return true;

        mReport += std::format("\n{}\n", written.error());
        fail();
        return false;
    }
}
