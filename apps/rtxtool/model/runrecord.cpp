#include "runrecord.hpp"

#include <algorithm>
#include <format>
#include <utility>

#include <components/files/conversion.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/renderer/renderer.hpp>

namespace RtxTool
{
    void RunRecord::begin(const SessionRequest& request)
    {
        mHeader.mSuite = request.mSuite;
        mHeader.mAsserts = Rtx::sAssertsOn;
        mHeader.mMeasures = request.mMeasures;
        mHeader.mMaps = request.mMaps;
        mHeader.mHashed = !request.mHashes.empty() || !request.mAgainst.empty() || !request.mPictures.empty()
            || std::any_of(
                request.mStops.begin(), request.mStops.end(), [](const Stop& stop) { return stop.mActions.mHash; });
        mHeader.mTurnsWeather = std::any_of(request.mStops.begin(), request.mStops.end(),
            [](const Stop& stop) { return !stop.mSky.mTurnThrough.empty(); });
        mHeader.mSetup = request.mSetup;
        mHeader.mStep = request.mStep;
    }

    void RunRecord::add(BenchPlace place)
    {
        // Read once the first place has loaded, which is the memory the frames walk.
        if (mPlaces.empty())
        {
            mHeader.mHugePageShare = Platform::Process::hugePageShare();
            mReport += describeHeader(mHeader);
        }
        mPlaces.push_back(std::move(place));
        mReport += describePlace(mPlaces.back(), mHeader.mMeasures);
    }

    void RunRecord::checked(const bool held)
    {
        ++mChecked;
        mFailed += held ? 0u : 1u;
        if (!held)
            fail();
    }

    SessionResult RunRecord::describe(const Stop* const left) const
    {
        SessionResult result;
        result.mExitStatus = mExitStatus != 0 ? mExitStatus : mDiffered ? sDifferedStatus : 0;
        result.mPlaces = mPlaces;
        result.mReport = mReport;
        if (left != nullptr)
            result.mLeft = *left;

        return result;
    }

    void RunRecord::finish(const SessionRequest& request)
    {
        mReport += describeTotal(mPlaces);

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

    void RunRecord::abandon(const SessionRequest& request)
    {
        fail();
        mHashes.dropUnpictured();
        finish(request);
    }

    bool RunRecord::wrote(const Misc::Result<void, std::string>& written)
    {
        if (written.isOk())
            return true;

        mReport += std::format("\n{}\n", written.error());
        fail();
        return false;
    }
}
