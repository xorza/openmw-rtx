#include "runrecord.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <components/files/conversion.hpp>
#include <components/platform/folder.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/common/asserts.hpp>

namespace RtxTool
{
    void RunRecord::begin(const SessionRequest& request)
    {
        mHeader.mSuite = request.mSuite;
        mHeader.mAsserts = Rtx::sAssertsOn;
        mHeader.mMeasures = request.mMeasures;
        mHeader.mPlayed = request.mPlayed;
        mKeepsPictures = request.mKeepsPictures;
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
            for (const std::filesystem::path& file : { actions.mFrameTimes, actions.mSheet, actions.mMapTile })
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

    void RunRecord::checked(const Check check, const bool held)
    {
        ++mChecked;
        if (held)
            return;
        ++mFailed;
        mFailedChecks.push_back(check);
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

        if (!request.mJson.empty())
        {
            if (wrote(writeJson(request.mJson, mHeader, mPlaces)))
                mReport += "wrote " + Files::pathToUnicodeString(request.mJson) + '\n';
        }
    }

    void RunRecord::save(const SessionRequest& request)
    {
        if (!request.mJson.empty())
            wrote(writeJson(request.mJson, mHeader, mPlaces));
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
