#include "crashinstall.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
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
#include <client/crash_report_database.h>
#include <handler/handler_main.h>
#include <handler/user_stream_data_source.h>
#include <util/file/file_writer.h>
#include <util/misc/uuid.h>

#include <components/files/conversion.hpp>
#include <components/platform/localtime.hpp>
#include <components/platform/process.hpp>

#include "crashhangwatch.hpp"
#include "crashmonitorarguments.hpp"
#include "crashmonitorlog.hpp"
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
        /// The game stood still for `mSeconds`, at the frame count `mStalledAt`, and had finished
        /// `mReportsBefore` hang reports when the watch asked for one more, which an End waits for
        /// until `mReportDue`.
        struct AskToEnd
        {
            std::uint32_t mSeconds;
            std::uint64_t mStalledAt;
            std::uint64_t mReportsBefore;
            std::chrono::steady_clock::time_point mReportDue;
        };

        /// Crashpad's handler returned `mResult`: the game is gone.
        struct HandlerReturned
        {
            int mResult;
        };

        /// **What the main thread is asked to do, in order.** It is the one thread that shows a dialog,
        /// which stands for as long as the player takes over it, so neither Crashpad's handler nor the
        /// watch waits on a click. The handler runs on a worker and the watch asks here.
        using Request = std::variant<AskToEnd, HandlerReturned>;

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

            /// How long the game stood still when the watch asked for a hang report.
            std::atomic<std::uint32_t> mStalledFor{ 0 };

            /// Ends the watch once Crashpad's handler returns, set on the handler's worker.
            std::mutex mWatchMutex;
            std::condition_variable mWatchWake;
            bool mWatchEnds = false;

            /// Every report of the session.
            SessionReports mReports;

            /// Whether the player's End ended the game: written and read on the main thread.
            bool mEnded = false;

            std::mutex mRequestMutex;
            std::condition_variable mRequestWake;
            std::vector<Request> mRequests;

            void post(const Request& request)
            {
                {
                    const std::lock_guard lock(mRequestMutex);
                    mRequests.push_back(request);
                }
                mRequestWake.notify_one();
            }

            /// The oldest request, waited for.
            Request take()
            {
                std::unique_lock lock(mRequestMutex);
                mRequestWake.wait(lock, [&] { return !mRequests.empty(); });
                const Request first = mRequests.front();
                mRequests.erase(mRequests.begin());
                return first;
            }
        };

        /// Whether the watch has ended, which it does once the game is gone.
        bool watchEnded(MonitorState& monitor)
        {
            const std::lock_guard lock(monitor.mWatchMutex);
            return monitor.mWatchEnds;
        }

        /// Whether the game finished the hang report `request` asked for, or is gone, by the time
        /// the report is due.
        bool awaitHangReport(MonitorState& monitor, const AskToEnd& request)
        {
            while (std::atomic_ref(monitor.mPage.get()->mHangReports).load(std::memory_order_acquire)
                <= request.mReportsBefore)
            {
                if (watchEnded(monitor))
                    return true;
                if (std::chrono::steady_clock::now() >= request.mReportDue)
                    return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return true;
        }

        /// Whether the player chose to end a game that stands still, as `request` says it does.
        bool askToEnd(MonitorState& monitor, const AskToEnd& request)
        {
            if (const EndAfter* const after = std::get_if<EndAfter>(&monitor.mAnswering))
            {
                // Until the report is finished or due: the End logs one that is still unfinished.
                if (after->mFrom == EndAfter::From::Report)
                    awaitHangReport(monitor, request);
                std::this_thread::sleep_for(after->mDelay);
                return true;
            }

            // Past a box that stood before this one, the game may have drawn again or gone.
            if (watchEnded(monitor) || std::atomic_ref(monitor.mPage.get()->mFrames).load() != request.mStalledAt)
                return false;

            const std::string message = monitor.mApplication + " has not drawn a frame for "
                + std::to_string(request.mSeconds)
                + " seconds. A report of what it is doing is being written; the log names it:\n"
                + Files::pathToUnicodeString(monitor.mLog.path()) + "\n\nWait for it, or end it?";
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

        /// Ends the game where it still stands where it stood when the player was asked: the box
        /// stands for as long as the player takes over it, and the game may have drawn again, or
        /// ended, in that time. **Not before the report the watch asked for is written**, which an
        /// End clicked while Crashpad still read the game would lose. Says in the log what it did.
        void endIfStillStalled(MonitorState& monitor, const AskToEnd& request)
        {
            const std::uint64_t stalledAt = request.mStalledAt;
            if (!watchEnded(monitor) && std::atomic_ref(monitor.mPage.get()->mFrames).load() == stalledAt
                && !awaitHangReport(monitor, request))
                monitor.mLog.write({ "Hang: the report did not finish in "
                    + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(sReportPatience).count())
                    + " seconds, and the game is ended without it" });

            const bool ended = watchEnded(monitor);

            if (!ended && std::atomic_ref(monitor.mPage.get()->mFrames).load() != stalledAt)
            {
                monitor.mLog.write({ "Hang: the game drew again before End was answered, and goes on" });
                return;
            }

            const Monitor::Ending ending = ended ? Monitor::Ending::Gone : monitor.mGame.end();
            switch (ending)
            {
                case Monitor::Ending::Ended:
                    monitor.mEnded = true;
                    return;
                case Monitor::Ending::Gone:
                    monitor.mLog.write({ "Hang: the game ended before End was answered, and nothing was ended" });
                    return;
                case Monitor::Ending::Failed:
                    monitor.mLog.write({ "Hang: the monitor could not end the game" });
                    return;
            }
        }

        /// **The hang watch**, once a second: reads the game for `HangWatch` and does what it says.
        void watch(MonitorState& monitor)
        {
            Heartbeat* const page = monitor.mPage.get();
            if (page == nullptr)
                return;

            HangWatch hangs(std::chrono::steady_clock::now());

            for (;;)
            {
                {
                    std::unique_lock lock(monitor.mWatchMutex);
                    if (monitor.mWatchWake.wait_for(lock, std::chrono::seconds(1), [&] { return monitor.mWatchEnds; }))
                        return;
                }

                const std::uint64_t frames = std::atomic_ref(page->mFrames).load();
                const std::uint32_t limit = std::atomic_ref(page->mHangSeconds).load();
                const auto now = std::chrono::steady_clock::now();

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

                monitor.mStalledFor = found->mSeconds;
                const std::uint64_t reportsBefore = std::atomic_ref(page->mHangReports).load(std::memory_order_acquire);
                std::atomic_ref(page->mHangAsked).fetch_add(1, std::memory_order_release);
                if (!std::holds_alternative<AskNobody>(monitor.mAnswering))
                    monitor.post(AskToEnd{ .mSeconds = found->mSeconds,
                        .mStalledAt = found->mStalledAt,
                        .mReportsBefore = reportsBefore,
                        .mReportDue = now + sReportPatience });
            }
        }

        /// **The package, said in the log** and on the monitor's errors, which a game started from a
        /// shell shares. Where it is, and empty where none was written.
        std::filesystem::path packageSession(MonitorState& monitor, std::span<const std::filesystem::path> dumps)
        {
            // A session that left no dump has nothing to package, and leaves no folder behind.
            if (dumps.empty())
                return {};

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

        /// **What the player is told once the game crashed or was ended**: the package, or where it
        /// could not be written, the dumps and the log it would have held. One button does the
        /// reporting: it opens a new issue filled in with the report, then the folder, which opens
        /// over the browser, so the file is there to be dragged in. A message box closes on any
        /// button, so one that does it all needs no second showing.
        void tellPlayer(const MonitorState& monitor, bool crashed, std::span<const std::filesystem::path> dumps,
            const std::filesystem::path& package, const LastReport& report)
        {
            const std::string title = monitor.mApplication + (crashed ? " has crashed" : " was ended");
            std::string message = crashed ? monitor.mApplication + " has crashed.\n\n"
                                          : monitor.mApplication + " stopped responding and was ended.\n\n";

            const bool packaged = !package.empty();
            const std::filesystem::path folder = packaged ? package.parent_path() : dumps.back().parent_path();
            if (packaged)
                message += "A report of what happened is saved in one file:\n" + Files::pathToUnicodeString(package);
            else
            {
                message += "A report is saved in\n";
                for (const std::filesystem::path& dump : dumps)
                    message += Files::pathToUnicodeString(dump) + "\n";
                message += "\nand the log says what happened:\n" + Files::pathToUnicodeString(monitor.mLog.path());
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
            if (ids.empty())
                return dumps;

            const std::unique_ptr<crashpad::CrashReportDatabase> database
                = crashpad::CrashReportDatabase::InitializeWithoutCreating(base::FilePath(monitor.mDatabase.native()));
            for (const crashpad::UUID& id : ids)
            {
                crashpad::CrashReportDatabase::Report report;
                if (database != nullptr
                    && database->LookUpCrashReport(id, &report) == crashpad::CrashReportDatabase::kNoError)
                    dumps.emplace_back(report.file_path.value());
                else
                    monitor.mLog.write({ "Crash package: the database holds no report " + id.ToString() });
            }
            return dumps;
        }

        /// **What a crash will meet**, asked through Crashpad's own database before its handler opens
        /// the same one: a report made in the folder a dump is written to, and dropped unfinished,
        /// which deletes it.
        MonitorAnswer probeDatabase(const std::filesystem::path& folder)
        {
            const std::unique_ptr<crashpad::CrashReportDatabase> database
                = crashpad::CrashReportDatabase::Initialize(base::FilePath(folder.native()));
            if (database == nullptr)
                return MonitorAnswer::DatabaseUnopened;

            std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
            if (database->PrepareNewCrashReport(&report) != crashpad::CrashReportDatabase::kNoError)
                return MonitorAnswer::ReportUnwritable;

            // Closed before the report goes: it removes its file before its writer closes it, and
            // Windows removes no file that is open, so each session kept a dump of nothing.
            report->Writer()->Close();
            return MonitorAnswer::Watching;
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

            // Before the handler, which ends at once on a database it cannot open, while the game would
            // take a monitor that started for a catcher that works.
            const MonitorAnswer answer = probeDatabase(monitor.mDatabase);
            monitor.mPage.answerGame(answer);
            if (answer != MonitorAnswer::Watching)
                return EXIT_FAILURE;

            crashpad::UserStreamDataSources sources;
            sources.push_back(std::make_unique<SummarySource>(SummarySource::Parts{ .mArguments = monitor,
                .mPage = monitor.mPage,
                .mStalledFor = monitor.mStalledFor,
                .mLog = monitor.mLog,
                .mReports = monitor.mReports }));

            std::vector<char*> handlerArgv;
            for (std::string& argument : handler)
                handlerArgv.push_back(argument.data());
            handlerArgv.push_back(nullptr);

            // The handler on a worker and the questions on this thread, `Request` says why.
            std::thread watchdog([&] { watch(monitor); });
            // The watch ends the moment the game is gone, so a question still standing then ends
            // nothing (`endIfStillStalled`).
            std::thread handlerThread([&] {
                const int returned
                    = crashpad::HandlerMain(static_cast<int>(handlerArgv.size() - 1), handlerArgv.data(), &sources);
                {
                    const std::lock_guard lock(monitor.mWatchMutex);
                    monitor.mWatchEnds = true;
                }
                monitor.mWatchWake.notify_one();
                monitor.post(HandlerReturned{ returned });
            });

            int result = 0;
            for (;;)
            {
                const Request request = monitor.take();
                if (const HandlerReturned* const returned = std::get_if<HandlerReturned>(&request))
                {
                    result = returned->mResult;
                    break;
                }
                const AskToEnd& asked = std::get<AskToEnd>(request);
                if (askToEnd(monitor, asked))
                    endIfStillStalled(monitor, asked);
            }

            watchdog.join();
            handlerThread.join();

            const SessionReports::Taken session = monitor.mReports.take();
            const std::vector<std::filesystem::path> dumps = findDumps(monitor, session.mIds);
            // **An end no handler inside the game saw**: a fail-fast — a failed security check, a
            // corrupted heap — ends the process past every filter, and leaves no dump where the
            // system's reporting did not ask for one (`Client::catchPastTheProcess`). Its exit code is
            // all there is to say what it was, where the system tells the monitor one.
            if (dumps.empty())
                if (const std::optional<std::uint32_t> code = monitor.mGame.exitCode(); code.has_value() && *code != 0)
                    monitor.mLog.write(
                        { "The game ended with " + Monitor::describeExitCode(*code) + " and left no dump" });

            // Before the package, which holds the log.
            monitor.mLog.flush();
            const std::filesystem::path package = packageSession(monitor, dumps);

            // Once the game is gone, so the box does not stand over a window that no longer draws.
            if (std::holds_alternative<AskThePlayer>(monitor.mAnswering) && (session.mCrashed || monitor.mEnded)
                && !dumps.empty())
                tellPlayer(monitor, session.mCrashed, dumps, package, session.mLast);

            return result;
        }
    }

    void runMonitorIfAsked(int argc, char** argv)
    {
        if (std::any_of(argv, argv + argc, [](const char* one) { return std::string_view(one) == sMonitorSwitch; }))
            std::exit(runMonitor(argc, argv));
    }
}
