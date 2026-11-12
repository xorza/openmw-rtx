#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <components/platform/sharedmemory.hpp>

namespace Crash
{
    /// How many bytes of a path the page holds, as UTF-8: the game's log, and the folder its
    /// packages go to. A longer path is not handed over, and the monitor then does without it.
    inline constexpr std::size_t sPathCapacity = 2048;

    /// **The one limit on a wait for the other side that is still running**: every wait between the
    /// game and its monitor ends when what it waits for comes or the other side is gone, and this is
    /// for a side that runs and never answers.
    inline constexpr std::chrono::seconds sLastResort{ 60 };

    /// What the monitor tells the game of itself, once, before Crashpad's handler runs.
    enum class MonitorAnswer : std::uint32_t
    {
        /// Nothing yet, as `SharedPage::create` leaves the page.
        Unanswered,

        /// The report database opened and took a report, and the handler runs on it.
        Watching,

        /// Crashpad could not open the report database.
        DatabaseUnopened,

        /// The report database opened and took no new report.
        ReportUnwritable,
    };

    /// What the game and its monitor share while both run, read and written through
    /// `std::atomic_ref`: plain words, so both sides map the same bytes.
    struct Heartbeat
    {
        /// Frames the game has drawn. The monitor calls a hang what leaves it unchanged too long.
        alignas(std::atomic_ref<std::uint64_t>::required_alignment) std::uint64_t mFrames;

        /// The thread that counted the last frame, as `Platform::Process::currentThreadId` gives it:
        /// the one a hang stopped, which a hang's report is about. Nought before the first frame.
        alignas(std::atomic_ref<std::uint64_t>::required_alignment) std::uint64_t mDrawing;

        /// The monitor's `MonitorAnswer`, stored once it has one, after `mMonitor`.
        alignas(std::atomic_ref<std::uint32_t>::required_alignment) std::uint32_t mMonitorAnswer;

        /// The monitor's process id, which the game lets read it on a system that asks for that.
        alignas(std::atomic_ref<std::uint32_t>::required_alignment) std::uint32_t mMonitor;

        /// How long unchanged is a hang, in seconds; nought turns the check off.
        alignas(std::atomic_ref<std::uint32_t>::required_alignment) std::uint32_t mHangSeconds;

        /// **Whether the game reached its end**, and what it ended with: `sEndReached` once
        /// `wrapApplication` returns, after `mExitCode`. A game gone without it was ended from outside
        /// or crashed: an out-of-memory kill or another's `SIGKILL`, which no handler inside it sees.
        alignas(std::atomic_ref<std::uint32_t>::required_alignment) std::uint32_t mEnd;
        alignas(std::atomic_ref<std::uint32_t>::required_alignment) std::uint32_t mExitCode;

        /// The game's log, which the monitor appends each summary to: nought until the game knows
        /// where it logs, which is after the catcher has started, and then how many bytes of
        /// `mLogPath` name it. Stored after the bytes, so a length the monitor reads covers a path
        /// already written.
        alignas(std::atomic_ref<std::uint32_t>::required_alignment) std::uint32_t mLogPathLength;
        char mLogPath[sPathCapacity];

        /// The folder the player's configuration names for the session's package, as the log
        /// path is handed over and after the same point: nought until then, and the monitor
        /// writes the package beside Crashpad's dumps.
        alignas(std::atomic_ref<std::uint32_t>::required_alignment) std::uint32_t mReportPathLength;
        char mReportPath[sPathCapacity];
    };

    /// What `Heartbeat::mEnd` holds once the game reached its end: a word no fresh page holds.
    inline constexpr std::uint32_t sEndReached = 0x454E4421;

    // An atomic that locks takes its lock in the process it runs in, which the other side never
    // sees: across processes only a lock-free one is atomic at all.
    static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);

    /// The shared page, named after the game's process id, so the monitor finds it from the id
    /// it is started with. Move-only; unmapped as it goes.
    class SharedPage
    {
    public:
        /// The game's side, made before the monitor starts. Null where the system refused.
        static SharedPage create(std::uint32_t process);

        /// The monitor's side of the page `process` made. Null where it is gone or never was.
        static SharedPage open(std::uint32_t process);

        Heartbeat* get() const { return static_cast<Heartbeat*>(mMemory.data()); }

        /// The monitor's answer where it has given one, or `Unanswered`.
        MonitorAnswer readAnswer() const;

        /// The monitor's process id, valid once it answered.
        std::uint32_t getMonitor() const;

        /// The monitor's side: tells the game whether it watches, and which process watches.
        /// Once, before the handler runs.
        void answerGame(MonitorAnswer answer, std::uint32_t monitor) const;

        /// The game's side, once `wrapApplication` returns: it reached its end, with `code`.
        void setEnd(int code) const;

        /// What the game ended with where it reached its end, or nothing where it did not.
        std::optional<int> getEnd() const;

        /// Hands the monitor the game's log, `log` in UTF-8, or says it cannot: no page, a path
        /// longer than `sPathCapacity`, or one handed over already. Once, from the thread that sets
        /// up the log.
        bool setLogPath(std::string_view log) const;

        /// The log the game handed over, in UTF-8, or empty where it has handed none yet.
        std::string getLogPath() const;

        /// Hands the monitor the folder the session's package goes to, as `setLogPath` hands the log.
        bool setReportPath(std::string_view folder) const;

        /// The folder the game handed over, in UTF-8, or empty where it has handed none.
        std::string getReportPath() const;

    private:
        Platform::SharedMemory mMemory;
    };
}
