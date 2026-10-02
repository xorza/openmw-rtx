#include "crashpage.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace Crash
{
    namespace
    {
        // Short, because macOS takes 31 characters of a shared memory name.
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
            if (path.size() > sPathCapacity)
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
