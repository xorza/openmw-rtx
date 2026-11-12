#include "crashpage.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace Crash
{
    namespace
    {
        // Short, because a system may limit a shared memory name to 31 characters.
        std::string nameOf(std::uint32_t process)
        {
            return "openmw-crash-" + std::to_string(process);
        }
    }

    SharedPage SharedPage::create(std::uint32_t process)
    {
        SharedPage page;
        page.mMemory = Platform::SharedMemory::create(nameOf(process), sizeof(Heartbeat));
        if (Heartbeat* const heartbeat = page.get())
            *heartbeat = Heartbeat{};
        return page;
    }

    MonitorAnswer SharedPage::readAnswer() const
    {
        Heartbeat* const page = get();
        assert(page != nullptr && "a monitor's answer read on a page the system refused");

        // A word past the last answer is none, because another process wrote it.
        const std::uint32_t word = std::atomic_ref(page->mMonitorAnswer).load(std::memory_order_acquire);
        return word <= static_cast<std::uint32_t>(MonitorAnswer::ReportUnwritable) ? static_cast<MonitorAnswer>(word)
                                                                                   : MonitorAnswer::Unanswered;
    }

    std::uint32_t SharedPage::getMonitor() const
    {
        Heartbeat* const page = get();
        return page != nullptr ? std::atomic_ref(page->mMonitor).load(std::memory_order_relaxed) : 0;
    }

    void SharedPage::answerGame(const MonitorAnswer answer, const std::uint32_t monitor) const
    {
        Heartbeat* const page = get();
        if (page == nullptr)
            return;
        std::atomic_ref(page->mMonitor).store(monitor, std::memory_order_relaxed);
        std::atomic_ref(page->mMonitorAnswer).store(static_cast<std::uint32_t>(answer), std::memory_order_release);
    }

    void SharedPage::setEnd(const int code) const
    {
        Heartbeat* const page = get();
        if (page == nullptr)
            return;
        std::atomic_ref(page->mExitCode).store(static_cast<std::uint32_t>(code), std::memory_order_relaxed);
        std::atomic_ref(page->mEnd).store(sEndReached, std::memory_order_release);
    }

    std::optional<int> SharedPage::getEnd() const
    {
        Heartbeat* const page = get();
        if (page == nullptr || std::atomic_ref(page->mEnd).load(std::memory_order_acquire) != sEndReached)
            return std::nullopt;
        return static_cast<int>(std::atomic_ref(page->mExitCode).load(std::memory_order_relaxed));
    }

    SharedPage SharedPage::open(std::uint32_t process)
    {
        SharedPage page;
        page.mMemory = Platform::SharedMemory::open(nameOf(process), sizeof(Heartbeat));
        return page;
    }

    namespace
    {
        bool writePath(std::uint32_t& length, char (&bytes)[sPathCapacity], const std::string_view path)
        {
            // **Once**: a second write would change the bytes under a length the monitor may be
            // reading them by.
            if (path.size() > sPathCapacity || std::atomic_ref(length).load(std::memory_order_relaxed) != 0)
                return false;

            std::memcpy(bytes, path.data(), path.size());
            std::atomic_ref(length).store(static_cast<std::uint32_t>(path.size()), std::memory_order_release);
            return true;
        }

        std::string readPath(std::uint32_t& length, const char (&bytes)[sPathCapacity])
        {
            // Clamped, because the length is a word another process wrote.
            const std::size_t read
                = std::min<std::size_t>(std::atomic_ref(length).load(std::memory_order_acquire), sPathCapacity);
            return std::string(bytes, read);
        }
    }

    bool SharedPage::setLogPath(const std::string_view log) const
    {
        Heartbeat* const page = get();
        return page != nullptr && writePath(page->mLogPathLength, page->mLogPath, log);
    }

    std::string SharedPage::getLogPath() const
    {
        Heartbeat* const page = get();
        return page != nullptr ? readPath(page->mLogPathLength, page->mLogPath) : std::string();
    }

    bool SharedPage::setReportPath(const std::string_view folder) const
    {
        Heartbeat* const page = get();
        return page != nullptr && writePath(page->mReportPathLength, page->mReportPath, folder);
    }

    std::string SharedPage::getReportPath() const
    {
        Heartbeat* const page = get();
        return page != nullptr ? readPath(page->mReportPathLength, page->mReportPath) : std::string();
    }
}
