#include "crash.hpp"
#include "crashinstall.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <base/files/file_path.h>
#include <client/crashpad_client.h>
#include <client/crashpad_info.h>
#include <client/simple_string_dictionary.h>
#include <client/simulate_crash.h>
#include <util/misc/tri_state.h>

#include <components/files/conversion.hpp>
#include <components/misc/result.hpp>
#include <components/platform/process.hpp>
#include <components/platform/thread.hpp>

#include "crashhangwatch.hpp"
#include "crashmonitorarguments.hpp"
#include "crashnote.hpp"
#include "crashpadclientsystem.hpp"
#include "crashpage.hpp"
#include "crashsummary.hpp"
#include "crashuncaught.hpp"

namespace Crash
{
    namespace
    {
        crashpad::CrashpadClient sClient;

        /// What `annotate` sets, registered with Crashpad at install. A static object and not the
        /// heap's, so a key set before install is there when the monitor starts.
        crashpad::SimpleStringDictionary sAnnotations;

        // Less the terminator, which `SetFromStringView` writes inside the value's bytes.
        static_assert(crashpad::SimpleStringDictionary::value_size - 1 == sAnnotationLength);

        /// The page this process shares with its monitor, from the moment the monitor started on
        /// it, and null before. **Never destroyed**: the reporter thread reads it and may outlive every
        /// static, and a page unmapped by static destruction under an `std::exit` was a fault inside a
        /// report. The process's end unmaps it, and the monitor unlinks its name when it opens it, so
        /// nothing of it stays in the system.
        std::atomic<SharedPage*> sPage{ nullptr };

        /// The page's heartbeat, or null before `install` succeeded.
        Heartbeat* sharedPage()
        {
            SharedPage* const page = sPage.load(std::memory_order_acquire);
            return page != nullptr ? page->get() : nullptr;
        }

        std::atomic<bool> sInstalled{ false };

        /// Locked, because pauses stand on any thread.
        std::mutex sHangMutex;
        HangLimit sHangLimit;

        /// Tells the page what the monitor watches by. Under `sHangMutex`.
        void showHangLimit()
        {
            if (Heartbeat* const page = sharedPage())
                std::atomic_ref(page->mHangSeconds).store(sHangLimit.shown(), std::memory_order_relaxed);
        }

        /// A dump of every thread and a summary, after which the game goes on. Nothing where a
        /// report is already being written: that one is already a dump of every thread, and this
        /// one would write over what it says.
        void reportAndContinue(ReportKind kind, std::string_view reason)
        {
            if (!beginReport(kind, reason))
                return;

            CRASHPAD_SIMULATE_CRASH();
            endReport();
        }

        /// How often the reporter thread reads the page. Against a hang limit of seconds, and a
        /// process's end waits no longer than this for a reporter with nothing to do.
        constexpr std::chrono::milliseconds sReporterWake{ 100 };

        /// Static and atomic, so they outlive the detached thread that reads them.
        std::atomic<bool> sReporterStops{ false };
        std::atomic<bool> sReporterStopped{ false };

        /// The process the reporter runs in: a fork keeps the exit handler and not the thread.
        std::uint32_t sReporterProcess = 0;

        /// **The one thread that takes a hang report, on every system.** Up before any hang, so a
        /// request starts nothing in a game that stands still: a thread the monitor started in the
        /// game took the loader lock first, and a signal landed on any thread, in a frame it shared
        /// with whatever that thread was doing. A poll, because the page is all the two processes
        /// share that works alike on every system.
        void takeHangReports(Heartbeat& page)
        {
            Platform::nameThisThread("crash reporter");
            std::uint64_t answered = 0;
            while (!sReporterStops.load(std::memory_order_acquire))
            {
                std::this_thread::sleep_for(sReporterWake);
                const std::uint64_t asked = std::atomic_ref(page.mHangAsked).load(std::memory_order_acquire);
                if (asked == answered)
                    continue;

                answered = asked;
                reportAndContinue(ReportKind::Hang, {});
                // Counted after the report and also where one being written already stood in for it: the
                // monitor's End waits on this, and a count that never came would hold it to its limit.
                std::atomic_ref(page.mHangReports).fetch_add(1, std::memory_order_release);
            }
            sReporterStopped.store(true, std::memory_order_release);
        }

        /// **No report outlives the statics it needs.** Crashpad deletes the lock a report on Windows
        /// holds as its statics go, which `~Lock` stopped with a breakpoint where a game ended during
        /// a report, and a request after that locked through a null pointer. Registered by `install`,
        /// after those statics were made, so this runs before they go. Detached and waited for with
        /// a limit, since a report whose monitor died never returns on Windows.
        void stopReporter()
        {
            if (Platform::Process::currentId() != sReporterProcess)
                return;

            sReporterStops.store(true, std::memory_order_release);
            const auto due = std::chrono::steady_clock::now() + sReportPatience;
            while (!sReporterStopped.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < due)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    void Client::onTerminate()
    {
        char reason[sNoteCapacity];
        endAsCrash(terminateReason(reason));
    }

    bool Client::isInstalled()
    {
        return sInstalled.load(std::memory_order_acquire);
    }

    Result<Installed, std::string_view> install(const Settings& settings)
    {
        static std::atomic<bool> tried{ false };
        if (tried.exchange(true))
            return Err{ "install was called before, and a process installs once" };

        // **The checks that need no page first**, so a refusal has nothing to undo.
        // The monitor is this executable, started again in its own mode.
        const std::optional<std::filesystem::path> self = Platform::Process::executable();
        if (!self.has_value())
            return Err{ "the system would not say which file this process runs" };

        // **The whole path, made here**: the monitor makes the last folder of it and no parent, and
        // on a fresh box the game starts before anything made the user data folder above it.
        std::error_code unmade;
        std::filesystem::create_directories(settings.mReportFolder, unmade);
        if (unmade)
            return Err{ "its report folder could not be made" };

        // A local until the monitor runs on it: a refusal from here on unmaps and unlinks it with
        // the local, and nothing reads a page with no monitor behind it.
        const std::uint32_t process = Platform::Process::currentId();
        SharedPage page = SharedPage::create(process);
        if (page.get() == nullptr)
            return Err{ "the page it shares with its monitor could not be made" };

        // Everything the monitor needs to know of this process, on its command line: it reads the
        // notes from here at a crash, as it reads the stacks, and the page by this process's id.
        MonitorArguments monitor;
        monitor.mClient = process;
        monitor.mNotes = reinterpret_cast<std::uint64_t>(noteTable().data());
        monitor.mAnswering = settings.mAnswering;
        monitor.mIssues = settings.mIssues;

        crashpad::CrashpadInfo* const info = crashpad::CrashpadInfo::GetCrashpadInfo();
        info->set_simple_annotations(&sAnnotations);

        // The object a crashed frame was working on is on the heap, and a dump of the stacks alone
        // names its address and nothing about it, which is what the RTX 2060 crash lacked. On
        // Windows Crashpad scans every stack for pointers and keeps what they point at, up to
        // 4 MiB; on Linux it keeps what the registers point at.
        info->set_gather_indirectly_referenced_memory(crashpad::TriState::kEnabled, 4 << 20);

        // **A report returns once its dump is written**, which the note table's kind and reason are put
        // back after (`endReport`): on Linux a request waits five seconds for the monitor unless it is
        // told to wait for the upload, and a monitor slow to begin then read the table put back, a hang
        // summarised as a crash. With no URL there is no upload, so the wait is for the dump alone; on
        // Windows and macOS a report waits for its dump already.
        constexpr bool waitForDump = true;
        const bool started = sClient.StartHandler(base::FilePath(self->native()),
            base::FilePath(settings.mReportFolder.native()), base::FilePath(), std::string(), std::string(),
            { { "product", settings.mApplication } }, monitor.write(), false, false, {}, base::FilePath(), waitForDump);

        // **Until the monitor has answered**, bounded: on Linux `StartHandler` answers once its fork
        // did, and a monitor that never ran left the game with no catcher and a log that said it had
        // one. Ten seconds, for a cold start of a debug build off a slow disk. Read where it failed as
        // well: on Windows a monitor that refuses ends before it hands its handler over,
        // so `StartHandler` fails, and only the page says why.
        switch (page.awaitMonitor(started ? std::chrono::milliseconds(10'000) : std::chrono::milliseconds(0)))
        {
            case MonitorAnswer::Watching:
                if (!started)
                    return Err{ "its monitor did not start" };
                break;
            case MonitorAnswer::Unanswered:
                return Err{ "its monitor did not start" };
            case MonitorAnswer::DatabaseUnopened:
                return Err{ "its report folder could not be opened" };
            case MonitorAnswer::ReportUnwritable:
                return Err{ "its report folder takes no new report" };
        }

        SharedPage* const kept = new SharedPage(std::move(page));
        sPage.store(kept, std::memory_order_release);
        {
            const std::lock_guard lock(sHangMutex);
            showHangLimit();
        }

        Client::keepConnectionToThisProcess();
        Client::prepareInstallingThread();
        std::set_terminate(Client::onTerminate);
        Client::hookEveryEnd();
        Installed installed;
        if (const std::string_view without = Client::catchPastTheProcess(sClient, *self); !without.empty())
            installed.mWithout.push_back(without);
        sInstalled = true;

        // After the flag above, so the thread-start hook gives this thread what it gives every other.
        Heartbeat& shared = *kept->get();
        sReporterProcess = process;
        try
        {
            std::thread([&shared] { takeHangReports(shared); }).detach();
            std::atexit(stopReporter);
        }
        catch (const std::system_error&)
        {
            installed.mWithout.push_back("hang reports: their thread could not start");
        }
        return installed;
    }

    void setLogFile(const std::filesystem::path& log)
    {
        if (!sInstalled)
            return;

        sPage.load(std::memory_order_acquire)->setLogPath(Files::pathToUnicodeString(log));
    }

    void setReportFolder(const std::filesystem::path& folder)
    {
        if (!sInstalled)
            return;

        sPage.load(std::memory_order_acquire)->setReportPath(Files::pathToUnicodeString(folder));
    }

    void setHangLimit(std::chrono::seconds limit)
    {
        const std::lock_guard lock(sHangMutex);
        sHangLimit.set(static_cast<std::uint32_t>(std::clamp<std::chrono::seconds::rep>(limit.count(), 0, 0xFFFFFFFF)));
        showHangLimit();
    }

    HangPause::HangPause()
    {
        const std::lock_guard lock(sHangMutex);
        sHangLimit.pause();
        showHangLimit();
    }

    HangPause::~HangPause()
    {
        const std::lock_guard lock(sHangMutex);
        Crash::contract(sHangLimit.resume(), "a hang pause ended that never began");
        showHangLimit();
    }

    void heartbeat()
    {
        // One writer, the thread that draws, so a load and a store rather than a locked add.
        if (Heartbeat* const page = sharedPage())
        {
            thread_local const std::uint64_t self = Platform::Process::currentThreadId();
            std::atomic_ref(page->mDrawing).store(self, std::memory_order_relaxed);
            std::atomic_ref frames(page->mFrames);
            frames.store(frames.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        }
    }

    void annotate(std::string_view key, std::string_view value)
    {
        sAnnotations.SetKeyValue(key, value);
    }

    void report(std::string_view reason)
    {
        if (!sInstalled)
            return;

        reportAndContinue(ReportKind::Report, reason);
    }

    void fatal(std::string_view reason)
    {
        if (!sInstalled)
            abortUncaught(reason);

        Client::endAsCrash(reason);
    }
}
