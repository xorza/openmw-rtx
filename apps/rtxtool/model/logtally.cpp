#include "logtally.hpp"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace RtxTool
{
    void LogTally::hear(const Debug::Level level, std::string_view message)
    {
        if (level != Debug::Error && level != Debug::Warning)
            return;
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
            message.remove_suffix(1);

        const std::scoped_lock lock(mMutex);
        const auto found = std::ranges::find_if(mLines, [&](const Line& line) { return line.mText == message; });
        if (found != mLines.end())
        {
            ++found->mCount;
            return;
        }
        if (mLines.size() == sMost)
        {
            ++mDropped;
            return;
        }
        mLines.push_back(Line{ .mError = level == Debug::Error,
            .mText = std::string(message),
            .mLeg = mLeg,
            .mFrame = mFrame,
            .mCount = 1 });
    }

    void LogTally::at(const std::string_view leg, const std::uint32_t frame)
    {
        const std::scoped_lock lock(mMutex);
        if (mLeg != leg)
            mLeg = leg;
        mFrame = frame;
    }

    void LogTally::drainInto(FindingTable& table)
    {
        const std::scoped_lock lock(mMutex);
        for (const Line& line : mLines)
            table.add(line.mError ? FindingKind::LogError : FindingKind::LogWarning, line.mText,
                FindingPlace{ .mLeg = line.mLeg, .mFrame = line.mFrame }, 0.0, line.mCount);
        if (mDropped > 0)
            table.add(
                FindingKind::LogWarning, "lines past the log's table", FindingPlace{ .mLeg = mLeg }, 0.0, mDropped);
        mLines.clear();
        mDropped = 0;
    }
}
