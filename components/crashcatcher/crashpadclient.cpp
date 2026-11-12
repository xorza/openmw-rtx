#include "crash.hpp"
#include "crashinstall.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <client/annotation.h>
#include <client/crashpad_client.h>
#include <client/crashpad_info.h>
#include <client/simple_string_dictionary.h>
#include <util/misc/tri_state.h>

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/result.hpp>
#include <components/platform/process.hpp>

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
        // Less the terminator, which `SetFromStringView` writes inside the value's bytes.
        static_assert(crashpad::SimpleStringDictionary::value_size - 1 == sAnnotationLength);

        /// **Everything the game's side of the catcher keeps**, on one owner that is never destroyed:
        /// a fault's handler, a terminate hook or a thread the statics outlive may reach it while the
        /// process ends, and a page unmapped by static destruction under an `std::exit` was a fault
        /// inside a report. The process's end unmaps the page, and the monitor unlinks its name when it
        /// opens it, so nothing of it stays in the system. Only the note table stays apart, in
        /// `crashnote.cpp`, because a signal handler writes the reason into it.
        struct Catcher
        {
            crashpad::CrashpadClient mClient;

            /// What `annotate` sets, registered with Crashpad at install: here from the first
            /// `annotate`, so a key set before install is there when the monitor starts.
            crashpad::SimpleStringDictionary mAnnotations;

            /// The note table, as an annotation every dump carries. Crashpad's interface takes a
            /// pointer it may write through; it reads the bytes only, and from the monitor.
            crashpad::Annotation mNotes{ crashpad::Annotation::UserDefinedType(1), sNotesAnnotation.data(),
                const_cast<std::byte*>(noteTable().data()) };

            /// The page this process shares with its monitor, from the moment the monitor answered on
            /// it.
            SharedPage mPage;

            /// The page's heartbeat, published once the page is the monitor's, and null before:
            /// `heartbeat` reads it on the thread that draws.
            std::atomic<Heartbeat*> mHeartbeat{ nullptr };

            std::atomic<bool> mInstalled{ false };

            /// Locked, because pauses stand on any thread.
            std::mutex mHangMutex;
            HangLimit mHangLimit;

            /// The thread that annotates, as the first `annotate` found it: `SimpleStringDictionary`
            /// locks nothing.
            std::atomic<std::uint64_t> mAnnotating{ 0 };

            Client::SystemState mSystem;
        };

        Catcher& catcher()
        {
            static Catcher* const kept = new Catcher;
            return *kept;
        }

        /// Tells the page what the monitor watches by. Under `mHangMutex`.
        void showHangLimit(Catcher& kept)
        {
            if (Heartbeat* const page = kept.mHeartbeat.load(std::memory_order_acquire))
                std::atomic_ref(page->mHangSeconds).store(kept.mHangLimit.shown(), std::memory_order_relaxed);
        }

        /// The monitor's answer, once it gives one, for as long as it runs. **No limit on a monitor
        /// that starts slowly**: a cold start of a debug build off a slow disk took seconds, and a
        /// limit ended the wait with Crashpad's handler installed and the log saying no catcher
        /// worked. `sLastResort` is for a monitor that runs and never answers.
        MonitorAnswer awaitMonitor(const SharedPage& page)
        {
            const auto due = std::chrono::steady_clock::now() + sLastResort;
            for (;;)
            {
                const MonitorAnswer answer = page.readAnswer();
                if (answer != MonitorAnswer::Unanswered || !Client::monitorRuns()
                    || std::chrono::steady_clock::now() >= due)
                    return answer;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    Client::SystemState& Client::systemState()
    {
        return catcher().mSystem;
    }

    void Client::sayUnreported(std::string_view reason)
    {
        Log(Debug::Error) << "Crash: the crash catcher's monitor has ended, and this crash goes unreported: " << reason;
    }

    void Client::onTerminate()
    {
        char reason[sNoteCapacity];
        endAsCrash(terminateReason(reason));
    }

    bool Client::isInstalled()
    {
        return catcher().mInstalled.load(std::memory_order_acquire);
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

        // A monitor that cannot read the game writes no dump, which a crash would find out too late.
        if (const std::string_view unreadable = Client::unreadable(); !unreadable.empty())
            return Err{ unreadable };

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
        // page by this process's id.
        MonitorArguments monitor;
        monitor.mClient = process;
        monitor.mAnswering = settings.mAnswering;
        monitor.mIssues = settings.mIssues;

        Catcher& kept = catcher();
        crashpad::CrashpadInfo* const info = crashpad::CrashpadInfo::GetCrashpadInfo();
        info->set_simple_annotations(&kept.mAnnotations);
        kept.mNotes.SetSize(static_cast<crashpad::Annotation::ValueSizeType>(noteTable().size()));

        // The object a crashed frame was working on is on the heap, and a dump of the stacks alone
        // names its address and nothing about it, which is what the RTX 2060 crash lacked. On
        // Windows Crashpad scans every stack for pointers and keeps what they point at, up to
        // 4 MiB; on Linux it keeps what the registers point at.
        info->set_gather_indirectly_referenced_memory(crashpad::TriState::kEnabled, 4 << 20);

        const bool started = kept.mClient.StartHandler(base::FilePath(self->native()),
            base::FilePath(settings.mReportFolder.native()), base::FilePath(), std::string(), std::string(),
            { { "product", settings.mApplication } }, monitor.write(), false, false);

        // Read where it failed as well: on Windows a monitor that refuses ends before it hands its
        // handler over, so `StartHandler` fails, and only the page says why.
        switch (started ? awaitMonitor(page) : page.readAnswer())
        {
            case MonitorAnswer::Watching:
                if (!started)
                    return Err{ "its monitor did not start" };
                break;
            case MonitorAnswer::Unanswered:
                return Err{ started && Client::monitorRuns() ? "its monitor did not answer"
                                                             : "its monitor did not start" };
            case MonitorAnswer::DatabaseUnopened:
                return Err{ "its report folder could not be opened" };
            case MonitorAnswer::ReportUnwritable:
                return Err{ "its report folder takes no new report" };
        }

        Installed installed;
        installed.mMonitor = page.getMonitor();
        installed.mReading = Client::describeReading();
        Client::watchMonitor(installed.mMonitor);
        if (const std::string_view unread = Client::letMonitorRead(installed.mMonitor); !unread.empty())
            installed.mWithout.push_back(unread);
        kept.mPage = std::move(page);
        kept.mHeartbeat.store(kept.mPage.get(), std::memory_order_release);
        {
            const std::lock_guard lock(kept.mHangMutex);
            showHangLimit(kept);
        }

        Client::keepConnectionToThisProcess();
        Client::prepareInstallingThread();
        std::set_terminate(Client::onTerminate);
        Client::hookEveryEnd();
        if (const std::string_view without = Client::catchPastTheProcess(kept.mClient, *self); !without.empty())
            installed.mWithout.push_back(without);
        kept.mInstalled = true;
        return installed;
    }

    void setLogFile(const std::filesystem::path& log)
    {
        if (Client::isInstalled())
            catcher().mPage.setLogPath(Files::pathToUnicodeString(log));
    }

    void setReportFolder(const std::filesystem::path& folder)
    {
        if (Client::isInstalled())
            catcher().mPage.setReportPath(Files::pathToUnicodeString(folder));
    }

    void reachedEnd(int code)
    {
        if (Client::isInstalled())
            catcher().mPage.setEnd(code);
    }

    std::vector<std::string_view> lostHooks()
    {
        std::vector<std::string_view> lost;
        if (Client::isInstalled())
            Client::lostHooks(lost);
        return lost;
    }

    void setHangLimit(std::chrono::seconds limit)
    {
        Catcher& kept = catcher();
        const std::lock_guard lock(kept.mHangMutex);
        kept.mHangLimit.set(
            static_cast<std::uint32_t>(std::clamp<std::chrono::seconds::rep>(limit.count(), 0, 0xFFFFFFFF)));
        showHangLimit(kept);
    }

    HangPause::HangPause()
    {
        Catcher& kept = catcher();
        const std::lock_guard lock(kept.mHangMutex);
        kept.mHangLimit.pause();
        showHangLimit(kept);
    }

    HangPause::~HangPause()
    {
        Catcher& kept = catcher();
        const std::lock_guard lock(kept.mHangMutex);
        Crash::contract(kept.mHangLimit.resume(), "a hang pause ended that never began");
        showHangLimit(kept);
    }

    void heartbeat()
    {
        // One writer, the thread that draws, so a load and a store rather than a locked add.
        if (Heartbeat* const page = catcher().mHeartbeat.load(std::memory_order_acquire))
        {
            thread_local const std::uint64_t self = Platform::Process::currentThreadId();
            std::atomic_ref(page->mDrawing).store(self, std::memory_order_relaxed);
            std::atomic_ref frames(page->mFrames);
            frames.store(frames.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        }
    }

    void annotate(std::string_view key, std::string_view value)
    {
        Catcher& kept = catcher();
        const std::uint64_t self = Platform::Process::currentThreadId();
        std::uint64_t first = 0;
        const bool owned = kept.mAnnotating.compare_exchange_strong(first, self, std::memory_order_acq_rel);
        assert((owned || first == self) && "Crash::annotate was called from a second thread");
        (void)owned;
        kept.mAnnotations.SetKeyValue(key, value);
    }

    void fatal(std::string_view reason)
    {
        if (!Client::isInstalled())
            abortUncaught(reason);

        Client::endAsCrash(reason);
    }
}
