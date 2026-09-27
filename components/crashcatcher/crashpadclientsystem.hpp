#pragma once

#include <filesystem>
#include <string_view>

#include "crashpage.hpp"

/// **The game's side of the catcher that each system spells its own way**, in
/// `crashpadclientposix.cpp` and `crashpadclientwin32.cpp`, and the three calls the shared half in
/// `crashpadclient.cpp` gives them back.
namespace Crash::Client
{
    /// This executable, which the monitor is started from: the running file, not `argv[0]`, which a
    /// shell may have given as a bare name or a relative path.
    std::filesystem::path executable();

    /// What the installing thread needs that every thread started after the catcher gets by itself:
    /// room to handle its own stack overflow. On Linux an alternate signal stack, which Crashpad's
    /// `pthread_create` gives the later ones; on Windows a stack guarantee, which the thread-start
    /// callback gives them.
    void prepareInstallingThread();

    /// Hooks every way this system ends a process that neither a fault nor `std::terminate` reaches,
    /// and the way the monitor asks for a hang report, which `page` carries where the monitor needs
    /// to be told it.
    void hookEveryEnd(Heartbeat& page);

    /// A crash for `reason`, taken here, whose dump is the stacks as they stand.
    [[noreturn]] void endAsCrash(std::string_view reason);

    /// A hang report, asked for by the monitor, which knows how long the game stood still. The
    /// shared half's, for the system's hook to call.
    void reportHang();

    /// **`std::terminate`, with the exception that called it**, which the fault it ends in names
    /// nothing of. The shared half's, for a system whose runtime keeps the hook per thread.
    void onTerminate();

    /// Whether the catcher is installed. The shared half's.
    bool isInstalled();
}
