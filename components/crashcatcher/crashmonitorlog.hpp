#pragma once

#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Crash
{
    class SharedPage;

    /// **What the monitor adds to the game's log, on a thread of its own**, stamped as the log stamps its own lines:
    /// a dump is written on Crashpad's thread, with the game stopped, and a log on a slow disk must not hold it. The
    /// game holds the file open, and shares its writing.
    ///
    /// **Nothing written before the game names its log is lost**: it waits until the game does, and
    /// where the game never does, as one that crashed while reading its configuration, `finish` puts
    /// it in a file of the monitor's own.
    class MonitorLog
    {
    public:
        /// Entries waiting at the most: a few a report, and reports are rare. Past it an entry is
        /// dropped, and counted among those `finish` says were not written.
        static constexpr std::size_t sMostWaiting = 64;

        /// `page` outlives this.
        explicit MonitorLog(const SharedPage& page);

        /// Writes what waits, and joins.
        ~MonitorLog();

        MonitorLog(const MonitorLog&) = delete;
        MonitorLog& operator=(const MonitorLog&) = delete;

        /// Queues `lines` as one entry, written in one write so that a line the game logs meanwhile
        /// lands before them or after them and never between two of them.
        void write(const std::vector<std::string>& lines);

        /// **Once the game is gone**: what waits goes to the game's log, or where the game never named
        /// one, to `fallback`, which is the log from then on. Waits for it, for a few seconds at the
        /// most. **Returns how many entries were not written**: dropped past `sMostWaiting`, refused by
        /// the file, or not yet written where the wait ran out or `fallback` could not be opened, so the
        /// monitor can say so where the log cannot.
        [[nodiscard]] std::size_t finish(const std::filesystem::path& fallback);

        /// The game's log, which it hands over through the page once it knows it, or the fallback
        /// `finish` took where it never did; empty before both, and where the page holds no path
        /// (`pathOf`).
        std::filesystem::path path() const;

        /// `text`, bytes the game wrote into the page, as a path, or empty where the system reads it as none: **a
        /// page a crash wrote over is the game's memory like any other**, and Windows refuses bytes that are no UTF-8
        /// with a throw, which on Crashpad's thread would end the monitor with the dump unwritten.
        static std::filesystem::path pathOf(const std::string& text);

    private:
        void run();

        const SharedPage& mPage;
        mutable std::mutex mMutex;
        std::filesystem::path mFallback;
        std::condition_variable mWake;
        std::condition_variable mEmptied;
        std::vector<std::string> mWaiting;
        std::size_t mUnwritten = 0;
        /// The entries the writer took out of `mWaiting` and has not finished writing.
        std::size_t mInFlight = 0;
        bool mEnds = false;

        /// Last, so that it starts on everything above.
        std::thread mThread;
    };
}
