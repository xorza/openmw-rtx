#include "crashmonitorlog.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <system_error>
#include <utility>

#include <components/files/conversion.hpp>
#include <components/platform/appendfile.hpp>
#include <components/platform/localtime.hpp>
#include <components/platform/thread.hpp>

#include "crashpage.hpp"

namespace Crash
{
    namespace
    {
        std::string stamp()
        {
            const auto now = std::chrono::system_clock::now();
            const std::tm local = Platform::localTime(std::chrono::system_clock::to_time_t(now)).value_or(std::tm{});
            const auto milliseconds
                = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
            char text[32];
            std::snprintf(text, sizeof(text), "[%02d:%02d:%02d.%03d E] ", local.tm_hour, local.tm_min, local.tm_sec,
                static_cast<int>(milliseconds));
            return text;
        }
    }

    MonitorLog::MonitorLog(const SharedPage& page)
        : mPage(page)
        , mThread([this] { run(); })
    {
    }

    MonitorLog::~MonitorLog()
    {
        {
            const std::lock_guard lock(mMutex);
            mEnds = true;
        }
        mWake.notify_one();
        mThread.join();
    }

    void MonitorLog::write(const std::vector<std::string>& lines)
    {
        const std::string at = stamp();
        std::string block;
        for (const std::string& line : lines)
            block += at + line + '\n';

        {
            const std::lock_guard lock(mMutex);
            if (mWaiting.size() >= sMostWaiting)
                return;
            mWaiting.push_back(std::move(block));
        }
        mWake.notify_one();
    }

    void MonitorLog::finish(const std::filesystem::path& fallback)
    {
        std::unique_lock lock(mMutex);
        if (pathOf(mPage.getLogPath()).empty() && !mWaiting.empty())
        {
            // A file of this session's alone, as the game's own log is.
            Platform::AppendFile::open(fallback, Platform::AppendStart::Emptied);
            mFallback = fallback;
        }
        mWake.notify_one();
        mEmptied.wait_for(lock, std::chrono::seconds(5), [&] { return mWaiting.empty() && !mWriting; });
    }

    std::filesystem::path MonitorLog::path() const
    {
        const std::filesystem::path named = pathOf(mPage.getLogPath());
        if (!named.empty())
            return named;
        const std::lock_guard lock(mMutex);
        return mFallback;
    }

    std::filesystem::path MonitorLog::pathOf(const std::string& text)
    {
        try
        {
            return Files::pathFromUnicodeString(text);
        }
        catch (const std::system_error&)
        {
            return {};
        }
    }

    void MonitorLog::run()
    {
        Platform::nameThisThread("crash log");
        std::unique_lock lock(mMutex);
        for (;;)
        {
            // **Read again while what waits has nowhere to go**: the game names its log through the
            // page, which wakes nothing here.
            const auto destination = [&] {
                const std::filesystem::path named = pathOf(mPage.getLogPath());
                return named.empty() ? mFallback : named;
            };
            mWake.wait_for(lock, std::chrono::milliseconds(200),
                [&] { return mEnds || (!mWaiting.empty() && !destination().empty()); });
            const std::filesystem::path log = destination();
            if (mWaiting.empty() && mEnds)
                return;
            if (mWaiting.empty() || log.empty())
            {
                // Nowhere to write it at the end, and nothing to wait for.
                if (mEnds)
                    return;
                continue;
            }

            std::vector<std::string> blocks;
            blocks.swap(mWaiting);
            mWriting = true;
            lock.unlock();

            for (const std::string& block : blocks)
                Platform::AppendFile::open(log, Platform::AppendStart::Kept).write(block);

            lock.lock();
            mWriting = false;
            if (mWaiting.empty())
                mEmptied.notify_all();
        }
    }
}
