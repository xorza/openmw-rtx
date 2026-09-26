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

    bool SharedPage::setLogPath(const std::string_view log) const
    {
        Heartbeat* const page = get();
        if (page == nullptr || log.size() > sLogPathCapacity)
            return false;

        std::memcpy(page->mLogPath, log.data(), log.size());
        std::atomic_ref(page->mLogPathLength).store(static_cast<std::uint32_t>(log.size()), std::memory_order_release);
        return true;
    }

    std::string SharedPage::getLogPath() const
    {
        Heartbeat* const page = get();
        if (page == nullptr)
            return {};

        // Clamped, because the length is a word another process wrote.
        const std::size_t length = std::min<std::size_t>(
            std::atomic_ref(page->mLogPathLength).load(std::memory_order_acquire), sLogPathCapacity);
        return std::string(page->mLogPath, length);
    }
}
