#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <handler/user_stream_data_source.h>
#include <util/misc/uuid.h>

#include "crashmonitorarguments.hpp"
#include "crashpage.hpp"
#include "crashsummary.hpp"

namespace Crash
{
    class MonitorLog;

    /// What the last report of a session was, which an issue is filled in with: the summary the
    /// public reads (`SummaryReaders::mPublic`).
    struct LastReport
    {
        std::string mTitle;
        std::vector<std::string> mSummary;
    };

    /// **The reports of a session**: written on Crashpad's thread, read on the one that ran it once the
    /// game is gone.
    class SessionReports
    {
    public:
        struct Taken
        {
            std::vector<crashpad::UUID> mIds;
            bool mCrashed = false;

            /// A crash is the last report of a session, and a hang the last of one the player ended.
            LastReport mLast;
        };

        void add(const crashpad::UUID& id, bool crashed);
        void setLast(LastReport last);
        Taken take();

    private:
        std::mutex mMutex;
        Taken mSession;
    };

    /// **The summary, written where the dump is**: Crashpad calls this with the snapshot the dump is
    /// written from, in this process and not the crashed one, on every system alike.
    class SummarySource final : public crashpad::UserStreamDataSource
    {
    public:
        struct Parts
        {
            const MonitorArguments& mArguments;
            const SharedPage& mPage;
            const std::atomic<std::uint32_t>& mStalledFor;
            MonitorLog& mLog;
            SessionReports& mReports;
        };

        explicit SummarySource(const Parts& parts)
            : mParts(parts)
        {
        }

        SummarySource(const SummarySource&) = delete;
        SummarySource& operator=(const SummarySource&) = delete;

        std::unique_ptr<crashpad::MinidumpUserExtensionStreamDataSource> ProduceStreamData(
            crashpad::ProcessSnapshot* snapshot) override;

    private:
        /// Fills `facts` from `process`, hands the player's summary to the log, keeps the public one
        /// for the issue, and answers the summary the dump carries, whose file is `dump`.
        std::string describe(const crashpad::ProcessSnapshot& process, const std::string& dump, CrashFacts& facts);

        const Parts mParts;
    };
}
