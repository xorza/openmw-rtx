#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace crashpad
{
    class CrashpadClient;
}

/// **The game's side of the catcher that each system spells its own way**, in
/// `crashpadclientposix.cpp` and `crashpadclientwin32.cpp`, and the two calls the shared half in
/// `crashpadclient.cpp` gives them back.
namespace Crash::Client
{
    /// **What this system's half keeps**, on the catcher's one owner: a hook the system calls with
    /// no context of its own reaches it through `systemState`.
    struct SystemState
    {
        /// The handlers the catcher's hooks were once `hookEveryEnd` set them, one a hook the
        /// system's half names, as addresses.
        std::array<std::uintptr_t, 8> mHooks{};

        /// What the system holds the monitor's process by, where it holds it by anything: a handle
        /// on Windows, from `watchMonitor` on; never closed, as the owner is never destroyed.
        std::intptr_t mMonitor = -1;
    };

    /// The one `SystemState`. The shared half's.
    SystemState& systemState();

    /// What the installing thread needs that every thread started after the catcher gets by itself:
    /// room to handle its own stack overflow. A stack guarantee on Windows, which the thread-start
    /// callback gives the later ones; nothing on POSIX, where Crashpad's install gives the installing
    /// thread its alternate signal stack itself.
    void prepareInstallingThread();

    /// **Keeps the connection to the monitor to this process**, so the monitor ends with it and not
    /// with the last program it started: the monitor serves the connection until every holder has
    /// closed it. On Linux the socket, which Crashpad makes inheritable; nothing on Windows, where the
    /// monitor ends once its last client process has, whatever holds that process's handles.
    void keepConnectionToThisProcess();

    /// Hooks every way this system ends a process that neither a fault nor `std::terminate` reaches,
    /// and keeps what the catcher's hooks are for `lostHooks`.
    void hookEveryEnd();

    /// The hooks `hookEveryEnd` and Crashpad set that another has since replaced, by name, into
    /// `into`.
    void lostHooks(std::vector<std::string_view>& into);

    /// **Why no monitor could read this process**, said for the log, or nothing where one can: on
    /// Linux Yama's `ptrace_scope` of 2 or more, which lets no process of the user's read another.
    /// Asked before the monitor starts, since every dump would fail at the crash.
    std::string_view unreadable();

    /// How the monitor reads this process, for the log: on Linux the Yama scope it reads under.
    std::string describeReading();

    /// Whether the monitor Crashpad started is still running, as far as this process can tell: on
    /// Linux the connection's far end is open, and on Windows the process `watchMonitor` holds has
    /// not ended, or none is held yet.
    bool monitorRuns();

    /// Holds the monitor `process`, once it has answered, for `monitorRuns` to ask.
    void watchMonitor(std::uint32_t process);

    /// **Lets the monitor `process` read this one at any time**, where the system asks for that:
    /// it takes a hang's dump without this process's help. On Linux Yama's scope 1 lets a process
    /// read only what it started, and the monitor was started by a double fork. What went wrong, for
    /// the log, or nothing.
    std::string_view letMonitorRead(std::uint32_t process);

    /// **What ends the process past every handler inside it**, handed to the system's own reporting
    /// where that has a way back to the monitor: on Windows, a fail-fast — a failed security check, a
    /// corrupted heap, `__fastfail` — which WER alone sees, through the module beside `executable`.
    /// What the catcher goes without where that could not be set up, for the log; nothing where it
    /// was, or where the system's handlers see every end already.
    std::string_view catchPastTheProcess(crashpad::CrashpadClient& client, const std::filesystem::path& executable);

    /// **Says in the log that a crash for `reason` goes unreported**, its monitor gone. The shared
    /// half's.
    void sayUnreported(std::string_view reason);

    /// A crash for `reason`, taken here, whose dump is the stacks as they stand. **At once where the
    /// monitor has ended**, with a line on the standard error, which the log tees: Crashpad on
    /// Windows waits a minute for a handler that is gone.
    [[noreturn]] void endAsCrash(std::string_view reason);

    /// **`std::terminate`, with the exception that called it**, which the fault it ends in names
    /// nothing of. The shared half's, for a system whose runtime keeps the hook per thread.
    void onTerminate();

    /// Whether the catcher is installed. The shared half's.
    bool isInstalled();
}
