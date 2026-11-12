#include "crashpadmonitorsystem.hpp"

#include <array>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <poll.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <client/crash_report_database.h>
#include <client/prune_crash_reports.h>
#include <handler/linux/crash_report_exception_handler.h>
#include <handler/linux/exception_handler_server.h>
#include <handler/prune_crash_reports_thread.h>
#include <snapshot/exception_snapshot.h>
#include <snapshot/linux/process_snapshot_linux.h>
#include <util/file/file_io.h>
#include <util/linux/direct_ptrace_connection.h>

#include <components/platform/wholefile.hpp>

#include "crashsummary.hpp"

namespace Crash::Monitor
{
    namespace
    {
        // `pidfd_open`'s number, which every architecture shares from Linux 5.3 on: AlmaLinux 8's kernel headers,
        // which the release builds against, name `pidfd_send_signal`, backported, and no `SYS_pidfd_open`. A
        // kernel older than 5.3 answers ENOSYS, as it would to the named one.
        constexpr long sPidfdOpen = 434;
    }

    GameProcess::GameProcess(std::uint32_t id)
        : mId(id)
    {
        // Through `syscall`, since the C library does not wrap a pidfd everywhere.
        mHold = Platform::UniqueHold<Closing>(syscall(sPidfdOpen, static_cast<pid_t>(id), 0));
    }

    void GameProcess::Closing::close(const Handle hold) noexcept
    {
        ::close(static_cast<int>(hold));
    }

    Ending GameProcess::end() const
    {
        // No descriptor is a kernel older than 5.3, where nothing is sent rather than something sent
        // to whatever process has the id now.
        if (mHold.get() < 0)
            return Ending::Failed;
        if (syscall(SYS_pidfd_send_signal, static_cast<int>(mHold.get()), SIGKILL, nullptr, 0) == 0)
            return Ending::Ended;
        return errno == ESRCH ? Ending::Gone : Ending::Failed;
    }

    std::optional<std::uint32_t> GameProcess::exitCode() const
    {
        return std::nullopt;
    }

    bool GameProcess::isGone() const
    {
        // A pidfd reads as ready once its process has ended.
        if (mHold.get() < 0)
            return true;
        pollfd polled{ .fd = static_cast<int>(mHold.get()), .events = POLLIN, .revents = 0 };
        return poll(&polled, 1, 0) != 0;
    }

    bool GameProcess::readWhole(
        const SnapshotStamp& stamp, const std::function<bool(const crashpad::ProcessSnapshot&)>& use) const
    {
        // Every thread is attached, and so stopped, as the snapshot reads it, and let go with the
        // connection.
        crashpad::DirectPtraceConnection connection;
        if (!connection.Initialize(static_cast<pid_t>(mId)))
            return false;
        crashpad::ProcessSnapshotLinux snapshot;
        if (!snapshot.Initialize(&connection))
            return false;
        snapshot.SetReportID(stamp.mReport);
        snapshot.SetClientID(stamp.mClient);
        for (const auto& [key, value] : stamp.mAnnotations)
            snapshot.AddAnnotation(key, value);
        return use(snapshot);
    }

    namespace
    {
        /// **Crashpad's handler with a hand on its dumps**: each crash's takes the lock a hang's dump
        /// takes, so the two never read the game through ptrace at once, which one of them would
        /// fail; and one whose dump failed is said.
        class HeldHandler final : public crashpad::ExceptionHandlerServer::Delegate
        {
        public:
            HeldHandler(crashpad::ExceptionHandlerServer::Delegate& handler, const HandlerParts& parts)
                : mHandler(handler)
                , mParts(parts)
            {
            }

            bool HandleException(pid_t client, uid_t user,
                const crashpad::ExceptionHandlerProtocol::ClientInformation& info, crashpad::VMAddress stack,
                pid_t* thread, crashpad::UUID* report) override
            {
                const std::lock_guard lock(mParts.mReading);
                return said(mHandler.HandleException(client, user, info, stack, thread, report));
            }

            bool HandleExceptionWithBroker(pid_t client, uid_t user,
                const crashpad::ExceptionHandlerProtocol::ClientInformation& info, int broker,
                crashpad::UUID* report) override
            {
                const std::lock_guard lock(mParts.mReading);
                return said(mHandler.HandleExceptionWithBroker(client, user, info, broker, report));
            }

            void AddAttachment(const base::FilePath& attachment) override { mHandler.AddAttachment(attachment); }
            void RemoveAttachment(const base::FilePath& attachment) override { mHandler.RemoveAttachment(attachment); }
            void RequestRetry() override { mHandler.RequestRetry(); }

        private:
            bool said(bool written)
            {
                if (!written)
                    mParts.mUnwritten.store(true, std::memory_order_release);
                return written;
            }

            crashpad::ExceptionHandlerServer::Delegate& mHandler;
            const HandlerParts& mParts;
        };

        /// The value of `--name=value` in `argument`, where it is that option.
        std::optional<std::string_view> valueOf(std::string_view argument, std::string_view name)
        {
            if (!argument.starts_with(name) || argument.size() <= name.size() || argument[name.size()] != '=')
                return std::nullopt;
            return argument.substr(name.size() + 1);
        }
    }

    int runHandler(std::vector<std::string>& arguments, const HandlerParts& parts)
    {
        // **What `StartHandler` on Linux hands its handler and nothing else**: the database, the
        // annotations, the socket and that it is shared. Anything more is a client this monitor
        // was not built with.
        base::FilePath database;
        std::map<std::string, std::string> annotations;
        int socket = -1;
        bool shared = false;
        for (std::size_t at = 1; at < arguments.size(); ++at)
        {
            const std::string_view argument = arguments[at];
            if (const auto path = valueOf(argument, "--database"))
                database = base::FilePath(std::string(*path));
            else if (const auto annotation = valueOf(argument, "--annotation"))
            {
                const std::size_t equals = annotation->find('=');
                if (equals == std::string_view::npos)
                {
                    LOG(ERROR) << "the monitor's handler was given an annotation with no value: " << argument;
                    return EXIT_FAILURE;
                }
                annotations.emplace(annotation->substr(0, equals), annotation->substr(equals + 1));
            }
            else if (const auto descriptor = valueOf(argument, "--initial-client-fd"))
            {
                const std::from_chars_result parsed
                    = std::from_chars(descriptor->data(), descriptor->data() + descriptor->size(), socket);
                if (parsed.ec != std::errc() || parsed.ptr != descriptor->data() + descriptor->size())
                {
                    LOG(ERROR) << "the monitor's handler was given no socket it reads: " << argument;
                    return EXIT_FAILURE;
                }
            }
            else if (argument == "--shared-client-connection")
                shared = true;
            else
            {
                LOG(ERROR) << "the monitor's handler was given what no client of this build gives: " << argument;
                return EXIT_FAILURE;
            }
        }
        if (database.empty() || socket < 0)
        {
            LOG(ERROR) << "the monitor's handler was given no database or no socket";
            return EXIT_FAILURE;
        }

        const std::unique_ptr<crashpad::CrashReportDatabase> reports
            = crashpad::CrashReportDatabase::Initialize(database);
        if (reports == nullptr)
            return EXIT_FAILURE;

        // As `HandlerMain` keeps the database within its default bounds.
        crashpad::PruneCrashReportThread prune(reports.get(), crashpad::PruneCondition::GetDefault());
        prune.Start();

        const std::vector<base::FilePath> attachments;
        crashpad::CrashReportExceptionHandler handler(reports.get(), nullptr, &annotations, &attachments, true, false,
            &parts.mSources, false, nullptr, nullptr, nullptr);
        HeldHandler held(handler, parts);

        crashpad::ExceptionHandlerServer server;
        const bool served = server.InitializeWithClient(crashpad::ScopedFileHandle(socket), shared);
        if (served)
            server.Run(&held);
        prune.Stop();
        return served ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    bool GameProcess::isHeld() const
    {
        // The state follows the name in parentheses, which may hold anything, so after the last of
        // them: `T` stopped, `t` stopped by a tracer.
        const std::string folder = "/proc/" + std::to_string(mId);
        std::string text;
        if (Platform::readWhole(folder + "/stat", text, Platform::sUnbounded).isOk())
        {
            const std::size_t named = text.rfind(')');
            if (named != std::string::npos && named + 2 < text.size()
                && (text[named + 2] == 'T' || text[named + 2] == 't'))
                return true;
        }

        // A tracer that lets the game run stands at a breakpoint as often as not.
        if (!Platform::readWhole(folder + "/status", text, Platform::sUnbounded).isOk())
            return false;
        constexpr std::string_view tracer = "\nTracerPid:";
        const std::size_t at = text.find(tracer);
        return at != std::string::npos && std::strtol(text.c_str() + at + tracer.size(), nullptr, 10) != 0;
    }

    std::string describeExitCode(std::uint32_t code)
    {
        return hex(code);
    }

    bool isCrashCode(std::uint32_t)
    {
        // No code reaches a monitor that is not the game's parent.
        return false;
    }

    std::string describeException(const crashpad::ExceptionSnapshot& exception, std::uint32_t process)
    {
        const std::uint32_t code = exception.Exception();
        if (code == 0xFFFFFFFFu)
            return {};

        static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 7> sNames{ {
            { SIGSEGV, "SIGSEGV" },
            { SIGBUS, "SIGBUS" },
            { SIGILL, "SIGILL" },
            { SIGFPE, "SIGFPE" },
            { SIGABRT, "SIGABRT" },
            { SIGTRAP, "SIGTRAP" },
            { SIGSYS, "SIGSYS" },
        } };
        std::string text = nameOf(sNames, code, "signal " + std::to_string(code));

        // A code of nought or less is a signal sent rather than a fault, `kill` or `raise`, whose
        // address is no fault's. Crashpad keeps the sender's id first among the codes of the signals
        // that carry one: the game's own, as `abort` sends it, says nothing more.
        if (static_cast<std::int32_t>(exception.ExceptionInfo()) <= 0)
        {
            const std::vector<std::uint64_t>& codes = exception.Codes();
            if (!codes.empty() && codes[0] != process)
                text += " sent by process " + std::to_string(codes[0]);
        }
        else if (code == SIGSEGV || code == SIGBUS)
            text += " at " + hex(exception.ExceptionAddress());
        return text;
    }
}
