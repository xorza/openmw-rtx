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

    /// **What the monitor adds to the game's log, on a thread of its own**, stamped as the log stamps
    /// its own lines. A dump is written on Crashpad's thread, with the game stopped, and a log on a slow
    /// disk must not hold it. The game holds the file open, and shares its writing.
    class MonitorLog
    {
    public:
        /// Entries waiting at the most: a few a report, and reports are rare. Past it an entry is
        /// dropped, since a monitor that cannot write its log has nothing better to do with it.
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

        /// Waits for what was queued before this, for a few seconds at the most.
        void flush();

        /// The game's log, which it hands over through the page once it knows it; empty before, and
        /// where the page holds no path (`pathOf`). An entry written before then is in its dump alone.
        std::filesystem::path path() const;

        /// `text`, bytes the game wrote into the page, as a path, or empty where the system reads it
        /// as none: **a page a crash wrote over is the game's memory like any other**, and Windows
        /// refuses bytes that are no UTF-8 with a throw, which on Crashpad's thread ended the monitor
        /// with the dump unwritten.
        static std::filesystem::path pathOf(const std::string& text);

    private:
        void run();

        const SharedPage& mPage;
        std::mutex mMutex;
        std::condition_variable mWake;
        std::condition_variable mEmptied;
        std::vector<std::string> mWaiting;
        bool mWriting = false;
        bool mEnds = false;

        /// Last, so that it starts on everything above.
        std::thread mThread;
    };
}
