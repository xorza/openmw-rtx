#include "crash.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>

#include <client/crashpad_client.h>
#include <client/crashpad_info.h>
#include <client/simple_string_dictionary.h>
#include <client/simulate_crash.h>

#include <components/files/conversion.hpp>
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

        SharedPage sPage;
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
    }

    void Client::onTerminate()
    {
        endAsCrash(terminateReason());
    }

    bool Client::isInstalled()
    {
        return sInstalled.load(std::memory_order_acquire);
    }

    std::optional<std::string> install(const Settings& settings)
    {
        static std::atomic<bool> tried{ false };
        if (tried.exchange(true))
            return "install was called before, and a process installs once";

        const std::uint32_t process = Platform::Process::currentId();
        sPage = SharedPage::create(process);
        if (sPage.get() == nullptr)
            return "the page it shares with its monitor could not be made";

        // Everything the monitor needs to know of this process, on its command line: it reads the
        // notes from here at a crash, as it reads the stacks, and the page by this process's id.
        MonitorArguments monitor;
        monitor.mClient = process;
        monitor.mNotes = reinterpret_cast<std::uint64_t>(noteTable().data());
        monitor.mNotesSize = noteTable().size();
        monitor.mApplication = settings.mApplication;
        monitor.mDialog = settings.mDialog;
        monitor.mEndAfter = settings.mEndAfter;
        monitor.mIssues = settings.mIssues;

        crashpad::CrashpadInfo* const info = crashpad::CrashpadInfo::GetCrashpadInfo();
        info->set_simple_annotations(&sAnnotations);

        // The object a crashed frame was working on is on the heap, and a dump of the stacks alone
        // names its address and nothing about it, which is what the RTX 2060 crash lacked. On
        // Windows Crashpad scans every stack for pointers and keeps what they point at, up to
        // 4 MiB; on Linux and macOS it keeps what the registers point at.
        info->set_gather_indirectly_referenced_memory(crashpad::TriState::kEnabled, 4 << 20);

        if (!sClient.StartHandler(base::FilePath(Client::executable().native()),
                base::FilePath(settings.mReportFolder.native()), base::FilePath(), std::string(), std::string(),
                { { "product", settings.mApplication } }, monitor.write(), false, false))
        {
            sPage = SharedPage();
            return "its monitor did not start";
        }

        Client::prepareInstallingThread();
        std::set_terminate(Client::onTerminate);
        Client::hookEveryEnd(*sPage.get());
        sInstalled = true;
        return {};
    }

    void setLogFile(const std::filesystem::path& log)
    {
        if (!sInstalled)
            return;

        sPage.setLogPath(Files::pathToUnicodeString(log));
    }

    void setHangLimit(std::chrono::seconds limit)
    {
        if (Heartbeat* const page = sPage.get())
            std::atomic_ref(page->mHangSeconds)
                .store(static_cast<std::uint32_t>(std::clamp<std::chrono::seconds::rep>(limit.count(), 0, 0xFFFFFFFF)),
                    std::memory_order_relaxed);
    }

    void heartbeat()
    {
        // One writer, the thread that draws, so a load and a store rather than a locked add.
        if (Heartbeat* const page = sPage.get())
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
