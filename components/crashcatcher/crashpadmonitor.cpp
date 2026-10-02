#include "crashinstall.hpp"
#include "crashnote.hpp"

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
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_misc.h>
#include <handler/handler_main.h>
#include <handler/user_stream_data_source.h>
#include <minidump/minidump_user_extension_stream_data_source.h>
#include <snapshot/cpu_context.h>
#include <snapshot/exception_snapshot.h>
#include <snapshot/memory_snapshot.h>
#include <snapshot/module_snapshot.h>
#include <snapshot/process_snapshot.h>
#include <snapshot/thread_snapshot.h>
#include <util/misc/uuid.h>
#include <util/process/process_memory.h>

#include <components/files/conversion.hpp>

#include "crashmonitorarguments.hpp"
#include "crashpackage.hpp"
#include "crashpadmonitorsystem.hpp"
#include "crashpage.hpp"
#include "crashsummary.hpp"

namespace Crash
{
    namespace
    {
        /// What the last report of a session was, which an issue is filled in with.
        struct LastReport
        {
            std::string mTitle;
            std::vector<std::string> mSummary;
        };

        /// What the game told the monitor on its command line, and what the monitor learnt since.
        struct MonitorState : MonitorArguments
        {
            explicit MonitorState(MonitorArguments arguments)
                : MonitorArguments(std::move(arguments))
                , mPage(SharedPage::open(mClient))
                , mGame(mClient)
            {
            }

            /// The game's log, which it hands over through the page once it knows it; empty before.
            std::filesystem::path getLog() const { return Files::pathFromUnicodeString(mPage.getLogPath()); }

            SharedPage mPage;
            Monitor::GameProcess mGame;

            /// How long the game stood still when the watch asked for a hang report.
            std::atomic<std::uint32_t> mStalledFor{ 0 };

            /// Ends the watch once Crashpad's handler returns, set on the handler's worker. A flag and
            /// not `std::stop_token`, which Apple's libc++ keeps behind its experimental switch.
            std::mutex mWatchMutex;
            std::condition_variable mWatchWake;
            bool mWatchEnds = false;

            /// The watch and the summary both write to the log, and a line each is what it takes.
            std::mutex mLogMutex;

            /// Every dump of the session, which the package holds, and whether one was a crash: written on
            /// Crashpad's thread, read on the one that ran it once the game is gone.
            std::mutex mReportMutex;
            std::vector<std::filesystem::path> mDumps;
            bool mCrashed = false;

            /// A crash is the last report of a session, and a hang the last of one the player ended.
            /// Guarded as the dumps are.
            LastReport mLastReport;

            /// Whether the player's End ended the game: written and read on the main thread.
            bool mEnded = false;

            /// **What the main thread is asked to do, in order.** It is the one thread that shows a
            /// dialog: on macOS a message box waits for the main dispatch queue, which Crashpad's
            /// Mach loop never drains, so the watch's box deadlocked the monitor whenever the
            /// handler held the main thread. The handler runs on a worker and the watch asks here.
            struct Request
            {
                enum class Kind
                {
                    /// The game stood still for `mSeconds`, at the frame count `mStalledAt`.
                    AskToEnd,

                    /// Crashpad's handler returned `mResult`: the game is gone.
                    HandlerReturned,
                };

                Kind mKind = Kind::AskToEnd;
                std::uint32_t mSeconds = 0;
                std::uint64_t mStalledAt = 0;
                int mResult = 0;
            };

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

        std::string stamp()
        {
            const auto now = std::chrono::system_clock::now();
            const std::tm local = Monitor::localTime(std::chrono::system_clock::to_time_t(now));
            const auto milliseconds
                = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
            char text[32];
            std::snprintf(text, sizeof(text), "[%02d:%02d:%02d.%03d E] ", local.tm_hour, local.tm_min, local.tm_sec,
                static_cast<int>(milliseconds));
            return text;
        }

        /// Appends `lines` to the game's log, stamped as the log stamps its own. The game holds the
        /// file open, and shares its writing.
        void appendToLog(MonitorState& monitor, const std::vector<std::string>& lines)
        {
            // Before the game has set its log up there is none, and a summary is in its dump alone.
            const std::filesystem::path path = monitor.getLog();
            if (path.empty())
                return;

            const std::lock_guard lock(monitor.mLogMutex);
            std::ofstream log(path, std::ios::app | std::ios::binary);
            const std::string at = stamp();
            for (const std::string& line : lines)
                log << at << line << '\n';
        }

        /// The module an address lies in, and the offset in it: "openmw.exe+0x112a9a7".
        std::string locate(const std::vector<const crashpad::ModuleSnapshot*>& modules, std::uint64_t address)
        {
            for (const crashpad::ModuleSnapshot* module : modules)
                if (address >= module->Address() && address - module->Address() < module->Size())
                {
                    const std::string path = module->Name();
                    const std::size_t slash = path.find_last_of("/\\");
                    return (slash == std::string::npos ? path : path.substr(slash + 1)) + "+"
                        + Monitor::hex(address - module->Address());
                }

            return {};
        }

        class Collect final : public crashpad::MemorySnapshot::Delegate
        {
        public:
            explicit Collect(std::vector<std::uint8_t>& into)
                : mInto(into)
            {
            }

            bool MemorySnapshotDelegateRead(void* data, size_t size) override
            {
                const auto* const bytes = static_cast<const std::uint8_t*>(data);
                mInto.assign(bytes, bytes + size);
                return true;
            }

        private:
            std::vector<std::uint8_t>& mInto;
        };

        /// Values on the faulting thread's stack, from its stack pointer up, that point into a
        /// module: the return addresses among them, and some that only look like one.
        void scanStack(const crashpad::ProcessSnapshot& snapshot, std::uint64_t thread,
            const crashpad::CPUContext& context, std::vector<std::string>& into)
        {
            constexpr std::size_t sCandidates = 16;

            for (const crashpad::ThreadSnapshot* one : snapshot.Threads())
            {
                const crashpad::MemorySnapshot* const stack = one->ThreadID() == thread ? one->Stack() : nullptr;
                if (stack == nullptr)
                    continue;

                std::vector<std::uint8_t> bytes;
                Collect collect(bytes);
                if (!stack->Read(&collect))
                    return;

                const std::size_t word = context.Is64Bit() ? 8 : 4;
                const std::uint64_t sp = context.StackPointer();
                std::size_t at = sp >= stack->Address() && sp - stack->Address() < bytes.size()
                    ? static_cast<std::size_t>(sp - stack->Address())
                    : 0;
                at -= at % word;

                const std::vector<const crashpad::ModuleSnapshot*> modules = snapshot.Modules();
                for (; at + word <= bytes.size() && into.size() < sCandidates; at += word)
                {
                    std::uint64_t value = 0;
                    std::copy_n(bytes.data() + at, word, reinterpret_cast<std::uint8_t*>(&value));
                    // A value spilled twice in a row is one address, and names one frame at most.
                    if (std::string where = locate(modules, value);
                        !where.empty() && (into.empty() || into.back() != where))
                        into.push_back(std::move(where));
                }
                return;
            }
        }

        class TextStream final : public crashpad::MinidumpUserExtensionStreamDataSource
        {
        public:
            explicit TextStream(std::string text)
                : MinidumpUserExtensionStreamDataSource(sSummaryStream)
                , mText(std::move(text))
            {
            }

            size_t StreamDataSize() override { return mText.size(); }

            bool ReadStreamData(Delegate* delegate) override
            {
                return delegate->ExtensionStreamDataSourceRead(mText.data(), mText.size());
            }

        private:
            std::string mText;
        };

        /// **The summary, written where the dump is**: Crashpad calls this with the snapshot the
        /// dump is written from, in this process and not the crashed one, on every system alike.
        class SummarySource final : public crashpad::UserStreamDataSource
        {
        public:
            explicit SummarySource(MonitorState& monitor)
                : mMonitor(monitor)
            {
            }

            std::unique_ptr<crashpad::MinidumpUserExtensionStreamDataSource> ProduceStreamData(
                crashpad::ProcessSnapshot* snapshot) override
            {
                CrashFacts facts;
                const crashpad::ExceptionSnapshot* const exception = snapshot->Exception();
                facts.mThread = exception != nullptr ? exception->ThreadID() : 0;

                std::vector<std::byte> table(mMonitor.mNotesSize);
                const crashpad::ProcessMemory* const memory = snapshot->Memory();
                const bool read
                    = memory != nullptr && !table.empty() && memory->Read(mMonitor.mNotes, table.size(), table.data());
                readNotes(read ? std::span<const std::byte>(table) : std::span<const std::byte>(), facts.mThread,
                    facts.mNotes);

                facts.mStalledFor = mMonitor.mStalledFor.load();

                if (exception != nullptr)
                {
                    facts.mException = Monitor::describeException(*exception, mMonitor.mClient);
                    if (const crashpad::CPUContext* const context = exception->Context())
                    {
                        facts.mWhere = locate(snapshot->Modules(), context->InstructionPointer());
                        scanStack(*snapshot, facts.mThread, *context, facts.mStack);
                    }
                }

                for (const auto& [key, value] : snapshot->AnnotationsSimpleMap())
                    facts.mAnnotations.emplace_back(key, value);
                for (const crashpad::ModuleSnapshot* module : snapshot->Modules())
                    for (const auto& [key, value] : module->AnnotationsSimpleMap())
                        facts.mAnnotations.emplace_back(key, value);

                crashpad::UUID report;
                snapshot->ReportID(&report);
                const std::filesystem::path dump
                    = mMonitor.mDatabase / Monitor::dumpFolder() / (report.ToString() + ".dmp");
                facts.mDump = Files::pathToUnicodeString(dump);

                std::vector<std::string> lines;
                summarise(facts, lines);
                appendToLog(mMonitor, lines);

                {
                    const std::lock_guard lock(mMonitor.mReportMutex);
                    mMonitor.mDumps.push_back(dump);
                    mMonitor.mCrashed = mMonitor.mCrashed || facts.mNotes.mKind == ReportKind::Crash;
                    mMonitor.mLastReport = { title(facts), lines };
                }

                std::string text;
                for (const std::string& line : lines)
                    text += line + '\n';
                return std::make_unique<TextStream>(std::move(text));
            }

        private:
            MonitorState& mMonitor;
        };

        /// Whether the player chose to end a game that stands still.
        bool askToEnd(const MonitorState& monitor, std::uint32_t seconds)
        {
            if (monitor.mEndAfter.has_value())
            {
                std::this_thread::sleep_for(*monitor.mEndAfter);
                return true;
            }

            const std::string message = monitor.mApplication + " has not drawn a frame for " + std::to_string(seconds)
                + " seconds. A report of what it is doing is being written; the log names it:\n"
                + Files::pathToUnicodeString(monitor.getLog()) + "\n\nWait for it, or end it?";
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
        /// ended, in that time. Says in the log what it did.
        void endIfStillStalled(MonitorState& monitor, std::uint64_t stalledAt)
        {
            bool ended = false;
            {
                const std::lock_guard lock(monitor.mWatchMutex);
                ended = monitor.mWatchEnds;
            }

            if (!ended && std::atomic_ref(monitor.mPage.get()->mFrames).load() != stalledAt)
            {
                appendToLog(monitor, { "Hang: the game drew again before End was answered, and goes on" });
                return;
            }

            if (ended || !monitor.mGame.end())
                appendToLog(monitor, { "Hang: the game ended before End was answered, and nothing was ended" });
            else
                monitor.mEnded = true;
        }

        /// **The hang watch**, once a second: a frame counter that stops for the limit is a hang,
        /// reported once until it moves again. It begins at the first frame, so a start that
        /// draws nothing for a while is not one.
        void watch(MonitorState& monitor)
        {
            Heartbeat* const page = monitor.mPage.get();
            if (page == nullptr)
                return;

            std::uint64_t last = 0;
            bool started = false;
            bool reported = false;
            auto since = std::chrono::steady_clock::now();

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
                const auto stalled = std::chrono::duration_cast<std::chrono::seconds>(now - since);

                if (frames != last || !started)
                {
                    if (reported)
                        appendToLog(
                            monitor, { "Hang: frames again after " + std::to_string(stalled.count()) + " seconds" });
                    started = started || frames != 0;
                    last = frames;
                    since = now;
                    reported = false;
                    continue;
                }

                if (limit == 0 || reported || stalled.count() < limit)
                    continue;

                reported = true;
                monitor.mStalledFor = static_cast<std::uint32_t>(stalled.count());
                monitor.mGame.requestHangReport(*page);
                if (monitor.mDialog)
                    monitor.post(MonitorState::Request{ .mKind = MonitorState::Request::Kind::AskToEnd,
                        .mSeconds = static_cast<std::uint32_t>(stalled.count()),
                        .mStalledAt = last });
            }
        }

        /// **The package, said in the log** and on the monitor's errors, which a game started from a
        /// shell shares. Where it is, and empty where none was written.
        std::filesystem::path packageSession(MonitorState& monitor, std::span<const std::filesystem::path> dumps)
        {
            const SessionPackage package = writeSessionPackage(monitor.mDatabase, monitor.mApplication,
                monitor.getLog(), dumps, Monitor::localTime(std::time(nullptr)));

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
            appendToLog(monitor, lines);
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
                message += "\nand the log says what happened:\n" + Files::pathToUnicodeString(monitor.getLog());
            }
            message += "\n\n";

            const bool issues = !monitor.mIssues.empty();
            const std::string attach
                = packaged ? Files::pathToUnicodeString(package.filename()) : "the log and the dump";
            if (issues)
                message += "Report the crash opens this folder and a new issue at\n" + monitor.mIssues
                    + "\nPlease attach " + (packaged ? "the file" : "the files") + " to it.";
            else
                message += packaged ? "Sending it helps to fix it." : "Sending them helps to fix it.";

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
            if (issues)
                SDL_OpenURL(newIssueUrl(monitor.mIssues, report.mTitle, report.mSummary, attach).c_str());
            SDL_OpenURL(folderUrl(folder).c_str());
        }
    }

    void runMonitorIfAsked(int argc, char** argv)
    {
        if (std::none_of(argv, argv + argc, [](const char* one) { return std::string_view(one) == sMonitorSwitch; }))
            return;

        std::vector<std::string> handler;
        MonitorState monitor(MonitorArguments::read(Monitor::commandLine(argc, argv), handler));

        crashpad::UserStreamDataSources sources;
        sources.push_back(std::make_unique<SummarySource>(monitor));

        std::vector<char*> handlerArgv;
        for (std::string& argument : handler)
            handlerArgv.push_back(argument.data());
        handlerArgv.push_back(nullptr);

        // The handler on a worker and the questions on this thread, `MonitorState::Request` says why.
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
            monitor.post(
                MonitorState::Request{ .mKind = MonitorState::Request::Kind::HandlerReturned, .mResult = returned });
        });

        int result = 0;
        for (;;)
        {
            const MonitorState::Request request = monitor.take();
            if (request.mKind == MonitorState::Request::Kind::HandlerReturned)
            {
                result = request.mResult;
                break;
            }
            if (askToEnd(monitor, request.mSeconds))
                endIfStillStalled(monitor, request.mStalledAt);
        }

        watchdog.join();
        handlerThread.join();

        std::vector<std::filesystem::path> dumps;
        bool crashed = false;
        LastReport report;
        {
            const std::lock_guard lock(monitor.mReportMutex);
            dumps = monitor.mDumps;
            crashed = monitor.mCrashed;
            report = monitor.mLastReport;
        }
        const std::filesystem::path package = packageSession(monitor, dumps);

        // Once the game is gone, so the box does not stand over a window that no longer draws.
        if (monitor.mDialog && (crashed || monitor.mEnded) && !dumps.empty())
            tellPlayer(monitor, crashed, dumps, package, report);

        std::exit(result);
    }
}
