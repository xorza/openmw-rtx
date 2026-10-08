#include "crash.hpp"
#include "crashinstall.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
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

        /// The page this process shares with its monitor, from the moment the monitor started on
        /// it, and null before. **Never destroyed**: the hang signal's handler and Windows' hang
        /// thread read it and outlive every static, and a page unmapped by static destruction under
        /// an `std::exit` was a fault inside the handler. The process's end unmaps it, and the
        /// monitor unlinks its name when it opens it, so nothing of it stays in the system.
        std::atomic<SharedPage*> sPage{ nullptr };

        /// The page's heartbeat, or null before `install` succeeded.
        Heartbeat* sharedPage()
        {
            SharedPage* const page = sPage.load(std::memory_order_acquire);
            return page != nullptr ? page->get() : nullptr;
        }

        std::atomic<bool> sInstalled{ false };

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
    }

    void Client::reportHang()
    {
        reportAndContinue(ReportKind::Hang, {});

        // Counted after the report and also where one being written already stood in for it: the
        // monitor's End waits on this, and a count that never came would hold it to its limit.
        if (Heartbeat* const page = sharedPage())
            std::atomic_ref(page->mHangReports).fetch_add(1, std::memory_order_release);
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

    Misc::Result<Installed, std::string_view> install(const Settings& settings)
    {
        static std::atomic<bool> tried{ false };
        if (tried.exchange(true))
            return Misc::Err{ "install was called before, and a process installs once" };

        // **The checks that need no page first**, so a refusal has nothing to undo.
        // The monitor is this executable, started again in its own mode.
        const std::optional<std::filesystem::path> self = Platform::Process::executable();
        if (!self.has_value())
            return Misc::Err{ "the system would not say which file this process runs" };

        // **The whole path, made here**: the monitor makes the last folder of it and no parent, and
        // on a fresh box the game starts before anything made the user data folder above it.
        std::error_code unmade;
        std::filesystem::create_directories(settings.mReportFolder, unmade);
        if (unmade)
            return Misc::Err{ "its report folder could not be made" };

        // A local until the monitor runs on it: a refusal from here on unmaps and unlinks it with
        // the local, and nothing reads a page with no monitor behind it.
        const std::uint32_t process = Platform::Process::currentId();
        SharedPage page = SharedPage::create(process);
        if (page.get() == nullptr)
            return Misc::Err{ "the page it shares with its monitor could not be made" };

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
        // 4 MiB; on Linux and macOS it keeps what the registers point at.
        info->set_gather_indirectly_referenced_memory(crashpad::TriState::kEnabled, 4 << 20);

        if (!sClient.StartHandler(base::FilePath(self->native()), base::FilePath(settings.mReportFolder.native()),
                base::FilePath(), std::string(), std::string(), { { "product", settings.mApplication } },
                monitor.write(), false, false))
            return Misc::Err{ "its monitor did not start" };

        SharedPage* const kept = new SharedPage(std::move(page));
        sPage.store(kept, std::memory_order_release);

        Client::keepConnectionToThisProcess();
        Client::prepareInstallingThread();
        std::set_terminate(Client::onTerminate);
        Client::hookEveryEnd(*kept->get());
        const Installed installed{ .mWithout = Client::catchPastTheProcess(sClient, *self) };
        sInstalled = true;
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
        if (Heartbeat* const page = sharedPage())
            std::atomic_ref(page->mHangSeconds)
                .store(static_cast<std::uint32_t>(std::clamp<std::chrono::seconds::rep>(limit.count(), 0, 0xFFFFFFFF)),
                    std::memory_order_relaxed);
    }

    void heartbeat()
    {
        // One writer, the thread that draws, so a load and a store rather than a locked add.
        if (Heartbeat* const page = sharedPage())
        {
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
