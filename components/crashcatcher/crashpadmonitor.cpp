#include "crashinstall.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_misc.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <client/crash_report_database.h>
#include <client/settings.h>
#include <handler/user_stream_data_source.h>
#include <minidump/minidump_file_writer.h>
#include <minidump/minidump_user_extension_stream_data_source.h>
#include <snapshot/process_snapshot.h>
#include <util/file/file_writer.h>
#include <util/misc/uuid.h>

#include <components/files/conversion.hpp>
#include <components/platform/appendfile.hpp>
#include <components/platform/localtime.hpp>
#include <components/platform/process.hpp>

#include "crashhangwatch.hpp"
#include "crashmonitorarguments.hpp"
#include "crashmonitorlog.hpp"
#include "crashmonitorrequests.hpp"
#include "crashnote.hpp"
#include "crashpackage.hpp"
#include "crashpadmonitorsystem.hpp"
#include "crashpadsummarysource.hpp"
#include "crashpage.hpp"
#include "crashsummary.hpp"

namespace Crash
{
    namespace
    {
        /// What the game told the monitor on its command line, and what the monitor learnt since.
        struct MonitorState : MonitorArguments
        {
            explicit MonitorState(MonitorArguments arguments)
                : MonitorArguments(std::move(arguments))
                , mPage(SharedPage::open(mClient))
                , mGame(mClient)
                , mLog(mPage)
            {
            }

            /// Where the session's package goes: the folder the game handed over, or beside the
            /// dumps where it handed none or the page holds no path.
            std::filesystem::path getReportFolder() const
            {
                const std::filesystem::path named = MonitorLog::pathOf(mPage.getReportPath());
                return named.empty() ? mDatabase : named;
            }

            SharedPage mPage;
            Monitor::GameProcess mGame;
            MonitorLog mLog;

            /// The report database, which a hang's dump is written into: opened once, before the
            /// handler opens its own on the same folder.
            std::unique_ptr<crashpad::CrashReportDatabase> mReportsOpen;

            /// Held while a dump reads the game (`Monitor::HandlerParts`).
            std::mutex mReading;

            /// Whether the handler took a crash and could not write its dump.
            std::atomic<bool> mUnwritten{ false };

            /// Ends the watch once Crashpad's handler returns, set on the handler's worker.
            std::mutex mWatchMutex;
            std::condition_variable mWatchWake;
            bool mWatchEnds = false;

            /// Every report of the session.
            SessionReports mReports;

            /// Whether the player's End ended the game: written and read on the main thread.
            bool mEnded = false;

            MonitorRequests mRequests;
        };

        /// Whether the watch has ended, which it does once the game is gone.
        bool watchEnded(MonitorState& monitor)
        {
            const std::lock_guard lock(monitor.mWatchMutex);
            return monitor.mWatchEnds;
        }

        /// **A hang's dump, taken from outside the game**, as Crashpad's `generate_dump` takes one,
        /// with the summary a crash's carries: into the database the handler writes crashes into, and
        /// counted with the session's reports. Whether it was written.
        bool dumpHang(MonitorState& monitor, SummarySource& summary, std::uint32_t stalledFor)
        {
            crashpad::CrashReportDatabase& reports = *monitor.mReportsOpen;
            std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
            if (reports.PrepareNewCrashReport(&report) != crashpad::CrashReportDatabase::kNoError)
                return false;

            Monitor::SnapshotStamp stamp{
                .mReport = report->ReportID(), .mClient = {}, .mAnnotations = { { "product", monitor.mApplication } }
            };
            if (crashpad::Settings* const settings = reports.GetSettings())
                settings->GetClientID(&stamp.mClient);

            const bool written = [&] {
                const std::lock_guard lock(monitor.mReading);
                return monitor.mGame.readWhole(stamp, [&](const crashpad::ProcessSnapshot& snapshot) {
                    crashpad::MinidumpFileWriter minidump;
                    minidump.InitializeFromSnapshot(&snapshot);
                    return minidump.AddUserExtensionStream(summary.produceHang(snapshot, stalledFor))
                        && minidump.WriteEverything(report->Writer());
                });
            }();
            if (!written)
                return false;

            crashpad::UUID id;
            if (reports.FinishedWritingCrashReport(std::move(report), &id) != crashpad::CrashReportDatabase::kNoError)
                return false;
            monitor.mReports.add(id, ReportKind::Hang);
            return true;
        }

        /// Whether the player chose to end a game that stands still, as `request` says it does.
        bool askToEnd(MonitorState& monitor, const AskToEnd& request)
        {
            if (const EndAfter* const after = std::get_if<EndAfter>(&monitor.mAnswering))
            {
                std::this_thread::sleep_for(after->mDelay);
                return true;
            }

            // Past a box that stood before this one, the game may have drawn again; one gone the caller asked.
            if (std::atomic_ref(monitor.mPage.get()->mFrames).load() != request.mStalledAt)
                return false;

            const std::string log = Files::pathToUnicodeString(monitor.mLog.path());
            const std::string message = monitor.mApplication + " has not drawn a frame for "
                + std::to_string(request.mSeconds) + " seconds. "
                + (request.mDumped ? "A report of what it is doing is saved; the log names it:\n"
                                   : "No report of what it is doing could be saved; the log says why:\n")
                + log + "\n\nWait for it, or end it?";
            const std::array<SDL_MessageBoxButtonData, 2> buttons{ {
                { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Wait" },
                { 0, 1, "End" },
            } };
            const std::string title = monitor.mApplication + " is not responding";
            const SDL_MessageBoxData box{ SDL_MESSAGEBOX_WARNING, nullptr, title.c_str(), message.c_str(),
                static_cast<int>(buttons.size()), buttons.data(), nullptr };

            int chosen = 0;
            return SDL_ShowMessageBox(&box, &chosen) && chosen == 1;
        }

        /// What became of a question the game ended before: whether the player was asked or not, End was
        /// never answered, and nothing was ended.
        void sayEndedFirst(MonitorState& monitor)
        {
            monitor.mLog.write({ "Hang: the game ended before End was answered, and nothing was ended" });
        }

        /// Ends the game where it still stands where it stood when the player was asked: the box
        /// stands for as long as the player takes over it, and the game may have drawn again, or
        /// ended, in that time. Says in the log what it did.
        void endIfStillStalled(MonitorState& monitor, const AskToEnd& request)
        {
            const bool gone = watchEnded(monitor) || monitor.mGame.isGone();
            if (!gone && std::atomic_ref(monitor.mPage.get()->mFrames).load() != request.mStalledAt)
            {
                monitor.mLog.write({ "Hang: the game drew again before End was answered, and goes on" });
                return;
            }

            switch (gone ? Monitor::Ending::Gone : monitor.mGame.end())
            {
                case Monitor::Ending::Ended:
                    monitor.mEnded = true;
                    return;
                case Monitor::Ending::Gone:
                    sayEndedFirst(monitor);
                    return;
                case Monitor::Ending::Failed:
                    monitor.mLog.write({ "Hang: the monitor could not end the game" });
                    return;
            }
        }

        /// **The hang watch**, once a second: reads the game for `HangWatch` and does what it says. And
        /// **the bound on Crashpad's handler**, which ends with the game: one still running
        /// `sLastResort` after the game is gone is overdue, and the monitor ends without it.
        void watch(MonitorState& monitor, SummarySource& summary)
        {
            Heartbeat* const page = monitor.mPage.get();
            if (page == nullptr)
                return;

            HangWatch hangs(std::chrono::steady_clock::now());
            std::optional<std::chrono::steady_clock::time_point> goneAt;

            for (;;)
            {
                {
                    std::unique_lock lock(monitor.mWatchMutex);
                    if (monitor.mWatchWake.wait_for(lock, std::chrono::seconds(1), [&] { return monitor.mWatchEnds; }))
                        return;
                }

                const auto now = std::chrono::steady_clock::now();
                if (monitor.mGame.isGone())
                {
                    goneAt = goneAt.value_or(now);
                    if (now - *goneAt < sLastResort)
                        continue;
                    monitor.mRequests.post(HandlerOverdue{});
                    return;
                }

                const std::uint64_t frames = std::atomic_ref(page->mFrames).load();
                const std::uint32_t limit = std::atomic_ref(page->mHangSeconds).load();

                // Asked only where it can matter: it reads the system, and a game that draws never
                // needs the answer.
                const bool held = !hangs.isNews(frames) && monitor.mGame.isHeld();
                const HangAnswer answer
                    = hangs.wake(HangWake{ .mNow = now, .mFrames = frames, .mLimitSeconds = limit, .mHeld = held });

                if (const HangEnded* const ended = std::get_if<HangEnded>(&answer))
                    monitor.mLog.write({ "Hang: frames again after " + std::to_string(ended->mSeconds) + " seconds" });

                const HangFound* const found = std::get_if<HangFound>(&answer);
                if (found == nullptr)
                    continue;

                const bool dumped = dumpHang(monitor, summary, found->mSeconds);
                if (!dumped)
                    monitor.mLog.write({ "Hang: no frame for " + std::to_string(found->mSeconds)
                        + " seconds, and no dump of it could be written" });
                // The dump held the game still, which is no stall of the game's own.
                hangs.wake(HangWake{ .mNow = std::chrono::steady_clock::now(),
                    .mFrames = frames,
                    .mLimitSeconds = limit,
                    .mHeld = true });

                if (!std::holds_alternative<AskNobody>(monitor.mAnswering))
                    monitor.mRequests.post(
                        AskToEnd{ .mSeconds = found->mSeconds, .mStalledAt = found->mStalledAt, .mDumped = dumped });
            }
        }

        /// **The package, said in the log** and on the monitor's errors, which a game started from a
        /// shell shares. Where it is, and empty where none was written.
        std::filesystem::path packageSession(MonitorState& monitor, std::span<const std::filesystem::path> dumps)
        {
            // A folder the configuration names may not exist yet; one that cannot be made fails the
            // package, which says so below.
            const std::filesystem::path folder = monitor.getReportFolder();
            std::error_code made;
            std::filesystem::create_directories(folder, made);
            const SessionPackage package = writeSessionPackage(folder, monitor.mApplication, monitor.mLog.path(), dumps,
                Platform::localTime(std::time(nullptr)).value_or(std::tm{}));

            std::vector<std::string> lines;
            for (const std::filesystem::path& missing : package.mMissing)
                lines.push_back("Crash package: " + Files::pathToUnicodeString(missing)
                    + " is not on disk, and the package goes without it");
            if (!package.mFailure.empty())
                lines.push_back("Crash package: none, " + package.mFailure);
            if (!package.mZip.empty())
            {
                const std::string named = Files::pathToUnicodeString(package.mZip);
                lines.push_back("Crash package: " + named);
                std::cerr << monitor.mApplication << ": the crash report is " << named << '\n';
            }
            monitor.mLog.write(lines);
            return package.mZip;
        }

        /// How the session ended, as the monitor tells the player.
        enum class Told
        {
            Crashed,

            /// Crashed, and the handler could not write the crash's dump.
            CrashedUnwritten,

            Ended,
        };

        /// **What the player is told once the game crashed or was ended**: the package, or where it
        /// could not be written, the dumps and the log it would have held. One button does the
        /// reporting: it opens a new issue filled in with the report, then the folder, which opens
        /// over the browser, so the file is there to be dragged in. A message box closes on any
        /// button, so one that does it all needs no second showing.
        void tellPlayer(const MonitorState& monitor, Told told, std::span<const std::filesystem::path> dumps,
            const std::filesystem::path& package, const LastReport& report)
        {
            const bool crashed = told != Told::Ended;
            const std::string title = monitor.mApplication + (crashed ? " has crashed" : " was ended");
            std::string message = crashed ? monitor.mApplication + " has crashed.\n\n"
                                          : monitor.mApplication + " stopped responding and was ended.\n\n";
            if (told == Told::CrashedUnwritten)
                message += "No dump of the crash could be written, and the log says what is known.\n\n";

            const std::filesystem::path log = monitor.mLog.path();
            const bool packaged = !package.empty();
            const std::filesystem::path folder = packaged ? package.parent_path()
                : dumps.empty()                           ? log.parent_path()
                                                          : dumps.back().parent_path();
            if (packaged)
                message += "A report of what happened is saved in one file:\n" + Files::pathToUnicodeString(package);
            else
            {
                if (!dumps.empty())
                {
                    message += "A report is saved in\n";
                    for (const std::filesystem::path& dump : dumps)
                        message += Files::pathToUnicodeString(dump) + "\n";
                    message += "\nand the log says what happened:\n";
                }
                else
                    message += "The log says what happened:\n";
                message += Files::pathToUnicodeString(log);
            }
            message += "\n\n";

            // A ray tracer leans on the newest paths through the driver, and a crash an old driver
            // causes is gone after an update, with no report filed.
            message
                += "An old graphics driver causes many of these. Update it and try again before you report this.\n\n";

            // **Said before the player is asked to share it**: a dump holds the game's memory, the
            // stacks and what they point to, and on Windows the process's whole environment, which
            // names the account and the computer; the log names the folders the game read. Nothing
            // makes a dump safe to post, so the player decides with what it holds in front of them.
            const std::string_view holds = packaged ? "It holds" : "They hold";
            message += std::string(holds)
                + " what the game had in memory, the folders it ran from, and on Windows the account's and "
                  "the computer's names.\n\n";

            const bool issues = !monitor.mIssues.empty();
            const std::string attach
                = packaged ? Files::pathToUnicodeString(package.filename()) : "the log and the dump";
            if (issues)
                message += "Report the crash opens this folder and a new issue at\n" + monitor.mIssues
                    + "\nThe issue is public: anyone can read what is attached to it. Attach "
                    + (packaged ? "the file" : "the files") + " only if you are content with that.";
            else
                message += packaged ? "Sending it helps to fix it. Send it only to someone you trust with that."
                                    : "Sending them helps to fix it. Send them only to someone you trust with that.";

            enum Button : int
            {
                Close,
                Report,
            };
            const std::array<SDL_MessageBoxButtonData, 2> buttons{ {
                { 0, Report, issues ? "Report the crash" : "Open the folder" },
                { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, Close, "Close" },
            } };
            const SDL_MessageBoxData box{ SDL_MESSAGEBOX_ERROR | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT, nullptr,
                title.c_str(), message.c_str(), static_cast<int>(buttons.size()), buttons.data(), nullptr };

            int chosen = Close;
            if (!SDL_ShowMessageBox(&box, &chosen) || chosen != Report)
                return;
            // A system that will not open an address as long as the one filled in gets the page with
            // its title alone, and the summary is in the package: Windows's `ShellExecute` refuses
            // one past about two thousand characters, which a summary of a few notes passes.
            if (issues && !SDL_OpenURL(newIssueUrl(monitor.mIssues, report.mTitle, report.mSummary, attach).c_str()))
                SDL_OpenURL(newIssueUrl(monitor.mIssues, report.mTitle, {}, attach).c_str());
            SDL_OpenURL(folderUrl(folder).c_str());
        }

        /// The dumps of `ids`, each where Crashpad's database says it is, in the order they were written.
        /// **Asked once the handler has ended**, since a dump moves as it is finished, and asked of
        /// the database because where its folders are is Crashpad's own business.
        std::vector<std::filesystem::path> findDumps(MonitorState& monitor, const std::vector<crashpad::UUID>& ids)
        {
            std::vector<std::filesystem::path> dumps;
            for (const crashpad::UUID& id : ids)
            {
                crashpad::CrashReportDatabase::Report report;
                if (monitor.mReportsOpen->LookUpCrashReport(id, &report) == crashpad::CrashReportDatabase::kNoError)
                    dumps.emplace_back(report.file_path.value());
                else
                    monitor.mLog.write({ "Crash package: the database holds no report " + id.ToString() });
            }
            return dumps;
        }

        /// **What a crash will meet**, asked through Crashpad's own database before its handler opens
        /// the same one: a report made in the folder a dump is written to, and dropped unfinished,
        /// which deletes it. The database stays open for the hang dumps.
        MonitorAnswer openReports(MonitorState& monitor)
        {
            monitor.mReportsOpen
                = crashpad::CrashReportDatabase::Initialize(base::FilePath(monitor.mDatabase.native()));
            if (monitor.mReportsOpen == nullptr)
                return MonitorAnswer::DatabaseUnopened;

            std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
            if (monitor.mReportsOpen->PrepareNewCrashReport(&report) != crashpad::CrashReportDatabase::kNoError)
                return MonitorAnswer::ReportUnwritable;

            // Closed before the report goes: it removes its file before its writer closes it, and
            // Windows removes no file that is open, so each session kept a dump of nothing.
            report->Writer()->Close();
            return MonitorAnswer::Watching;
        }

        /// **Crashpad's own warnings and errors, in the log**, where a player's report carries them:
        /// a failed ptrace or database write went to an inherited standard error no one reads. Through
        /// a pointer of its own, as Crashpad's hook is a function and takes no context.
        MonitorLog* sCrashpadLog = nullptr;

        bool logCrashpad(logging::LogSeverity severity, const char*, int, std::size_t start, const std::string& text)
        {
            if (severity < logging::LOG_WARNING || sCrashpadLog == nullptr || start > text.size())
                return false;
            std::string_view said = std::string_view(text).substr(start);
            while (!said.empty() && (said.back() == '\n' || said.back() == '\r'))
                said.remove_suffix(1);
            sCrashpadLog->write({ "Crashpad: " + std::string(said) });
            return false;
        }

        /// Routes Crashpad's log into `log` while it stands.
        class CrashpadLogged
        {
        public:
            explicit CrashpadLogged(MonitorLog& log)
            {
                sCrashpadLog = &log;
                logging::SetLogMessageHandler(logCrashpad);
            }

            ~CrashpadLogged()
            {
                logging::SetLogMessageHandler(nullptr);
                sCrashpadLog = nullptr;
            }

            CrashpadLogged(const CrashpadLogged&) = delete;
            CrashpadLogged& operator=(const CrashpadLogged&) = delete;
            CrashpadLogged(CrashpadLogged&&) = delete;
            CrashpadLogged& operator=(CrashpadLogged&&) = delete;
        };

        /// The monitor's last line on the session: how the game ended and how many reports it left.
        std::string describeEnd(MonitorState& monitor, std::size_t reports, bool reported)
        {
            const std::string left
                = ", and the session left " + std::to_string(reports) + (reports == 1 ? " report" : " reports");
            if (const std::optional<int> code = monitor.mPage.getEnd())
                return "The game reached its end with exit code " + std::to_string(*code) + left;
            if (const std::optional<std::uint32_t> code = monitor.mGame.exitCode())
                return "The game ended with " + Monitor::describeExitCode(*code) + " before reaching its end" + left;
            if (reported)
                return "The game ended before reaching its end" + left;
            // **An end no handler inside the game saw**, which only the missing word says: an
            // out-of-memory kill, or another process's `SIGKILL`.
            return "The game ended without reaching its end and left no report: the system or another program "
                   "ended it";
        }
    }

    namespace
    {
        /// The monitor's whole life, and what the process exits with. **A function that returns**, so
        /// its parts end before `std::exit`, which runs no local's destructor and took the log's thread
        /// with what it still held: the line that names the package, on Windows most of the time.
        int runMonitor(int argc, char** argv)
        {
            std::vector<std::string> handler;
            MonitorState monitor(MonitorArguments::read(Platform::Process::commandLine(argc, argv), handler));
            const CrashpadLogged crashpadLogged(monitor.mLog);

            // Before the handler, which ends at once on a database it cannot open, while the game would
            // take a monitor that started for a catcher that works.
            const MonitorAnswer answer = openReports(monitor);
            monitor.mPage.answerGame(answer, Platform::Process::currentId());
            if (answer != MonitorAnswer::Watching)
                return EXIT_FAILURE;

            auto owned = std::make_unique<SummarySource>(SummarySource::Parts{
                .mArguments = monitor, .mPage = monitor.mPage, .mLog = monitor.mLog, .mReports = monitor.mReports });
            SummarySource& summary = *owned;
            crashpad::UserStreamDataSources sources;
            sources.push_back(std::move(owned));

            // The handler on a worker and the questions on this thread, `MonitorRequest` says why.
            std::thread watchdog([&] { watch(monitor, summary); });
            // The watch ends the moment the game is gone, so a question still standing then ends
            // nothing (`endIfStillStalled`).
            std::thread handlerThread([&] {
                const int returned = Monitor::runHandler(handler,
                    Monitor::HandlerParts{
                        .mSources = sources, .mReading = monitor.mReading, .mUnwritten = monitor.mUnwritten });
                {
                    const std::lock_guard lock(monitor.mWatchMutex);
                    monitor.mWatchEnds = true;
                }
                monitor.mWatchWake.notify_one();
                monitor.mRequests.post(HandlerReturned{ returned });
            });

            int result = EXIT_FAILURE;
            bool overdue = false;
            for (;;)
            {
                const MonitorRequest request = monitor.mRequests.take();
                if (const HandlerReturned* const returned = std::get_if<HandlerReturned>(&request))
                {
                    result = returned->mResult;
                    break;
                }
                if (std::holds_alternative<HandlerOverdue>(request))
                {
                    overdue = true;
                    monitor.mLog.write(
                        { "Crash: Crashpad's handler did not end within " + std::to_string(sLastResort.count())
                            + " seconds of the game, and the monitor ends without it" });
                    break;
                }
                const AskToEnd& asked = std::get<AskToEnd>(request);
                // Gone before its question was asked, which no box asks about.
                if (watchEnded(monitor) || monitor.mGame.isGone())
                {
                    sayEndedFirst(monitor);
                    continue;
                }
                if (askToEnd(monitor, asked))
                    endIfStillStalled(monitor, asked);
            }

            watchdog.join();

            // **Every question a hang raised is said once**, in whichever order the game's end and the question
            // reached this thread: a hang's dump outlasted the game on a loaded machine, the question came after
            // the end, and the log said nothing of it.
            for (std::size_t unasked = monitor.mRequests.takeUnasked(); unasked > 0; --unasked)
                sayEndedFirst(monitor);

            // A handler stuck past the bound is left to the process's end, below.
            if (overdue)
                handlerThread.detach();
            else
                handlerThread.join();

            const SessionReports::Taken session = monitor.mReports.take();
            const std::vector<std::filesystem::path> dumps = findDumps(monitor, session.mIds);

            // **A crash whose dump was not written**: one the handler failed, or on Windows an end past
            // every handler, a failed security check or a corrupted heap, which leaves no dump where the
            // system's reporting did not ask for one (`Client::catchPastTheProcess`).
            const std::optional<std::uint32_t> code = monitor.mGame.exitCode();
            const bool unwritten = monitor.mUnwritten.load(std::memory_order_acquire)
                || (!session.mCrashed && code.has_value() && Monitor::isCrashCode(*code));
            if (unwritten)
                monitor.mLog.write({ "Crash: the game crashed, and no dump of it could be written" });
            monitor.mLog.write({ describeEnd(monitor, session.mIds.size(), session.mCrashed || unwritten) });

            // Before the package, which holds the log: where the game never named one, the monitor's
            // lines go to a file of its own beside the reports, which the package then holds.
            const std::filesystem::path own = monitor.getReportFolder() / (monitor.mApplication + "-monitor.log");
            if (const std::size_t lost = monitor.mLog.finish(own); lost > 0)
            {
                // Where the log cannot say it, as the package's path is said below.
                const std::filesystem::path log = monitor.mLog.path();
                std::cerr << monitor.mApplication << ": " << lost
                          << " entries of the crash monitor's log could not be written to "
                          << Files::pathToUnicodeString(log.empty() ? own : log) << '\n';
            }
            const bool told = session.mCrashed || unwritten || monitor.mEnded;
            const std::filesystem::path package
                = told || !dumps.empty() ? packageSession(monitor, dumps) : std::filesystem::path();

            // Once the game is gone, so the box does not stand over a window that no longer draws.
            if (std::holds_alternative<AskThePlayer>(monitor.mAnswering) && told)
            {
                const LastReport last = session.mLast.mTitle.empty() && unwritten
                    ? LastReport{ "Crash: no dump could be written", {} }
                    : session.mLast;
                tellPlayer(monitor,
                    unwritten              ? Told::CrashedUnwritten
                        : session.mCrashed ? Told::Crashed
                                           : Told::Ended,
                    dumps, package, last);
            }

            // **Ended here where the handler is stuck**: it still reads what this function's locals
            // hold, which returning would destroy under it. The log is finished.
            if (overdue)
            {
                std::fflush(nullptr);
                std::_Exit(EXIT_FAILURE);
            }
            return result;
        }
    }

    void runMonitorIfAsked(int argc, char** argv)
    {
        if (std::any_of(argv, argv + argc, [](const char* one) { return std::string_view(one) == sMonitorSwitch; }))
            std::exit(runMonitor(argc, argv));
    }
}
