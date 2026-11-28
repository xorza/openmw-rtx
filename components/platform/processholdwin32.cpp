#include "processhold.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>

#include <components/misc/windows.hpp>

namespace Platform
{
    namespace
    {
        HANDLE handleOf(std::intptr_t hold)
        {
            // A handle is held as the integer `UniqueHold` keeps; this turns it back.
            return hold < 0 ? nullptr : reinterpret_cast<HANDLE>(hold);
        }
    }

    ProcessHold::ProcessHold(std::uint32_t id)
        : mId(id)
    {
        // Ending and waiting need `PROCESS_TERMINATE` and `SYNCHRONIZE`; the rest are what `ProcessSnapshotWin`
        // reads a hang's dump with: the memory, a suspension, and the exit code.
        const HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_SUSPEND_RESUME
                | PROCESS_DUP_HANDLE | PROCESS_TERMINATE | SYNCHRONIZE,
            FALSE, static_cast<DWORD>(id));
        // The handle kept as the integer `UniqueHold` holds.
        if (handle != nullptr)
            mHeld = UniqueHold<Closing>(reinterpret_cast<std::intptr_t>(handle));
    }

    void ProcessHold::Closing::close(const Handle hold) noexcept
    {
        CloseHandle(handleOf(hold));
    }

    ProcessEnd ProcessHold::end() const
    {
        const HANDLE handle = handleOf(native());
        if (handle == nullptr)
            return ProcessEnd::Failed;
        if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0)
            return ProcessEnd::Gone;
        return TerminateProcess(handle, 3) != FALSE ? ProcessEnd::Ended : ProcessEnd::Failed;
    }

    bool ProcessHold::waitForEnd(std::chrono::milliseconds most) const
    {
        assert(most >= std::chrono::milliseconds::zero());
        const HANDLE handle = handleOf(native());
        if (handle == nullptr)
            return true;
        // `INFINITE` is the one wait that never ends, so a bound stops short of it.
        const DWORD wait = static_cast<DWORD>(std::clamp<std::int64_t>(most.count(), 0, INFINITE - 1));
        return WaitForSingleObject(handle, wait) != WAIT_TIMEOUT;
    }
}
