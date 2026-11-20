#include "filewriter.hpp"

#include <fstream>
#include <ios>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <components/files/conversion.hpp>
#include <components/misc/result.hpp>

#include "reportfile.hpp"

namespace RtxTool
{
    namespace
    {
        Result<void, std::string> append(const std::filesystem::path& path, const std::string& text)
        {
            std::ofstream file(path, std::ios::binary | std::ios::app);
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            file.flush();
            if (!file)
                return Err{ "could not add to " + Files::pathToUnicodeString(path) };
            return {};
        }
    }

    FileWriter::FileWriter()
    {
        mPending.reserve(sPendingMost);
        mFailures.reserve(sFailuresMost);
        // Started once what it reads stands: begun in the initializer list, it read the queue the lines above
        // were still reserving.
        mWorker = std::jthread([this](const std::stop_token& stop) { serve(stop); });
    }

    void FileWriter::write(std::filesystem::path path, std::string text, const As as)
    {
        std::unique_lock lock(mMutex);
        mChanged.wait(lock, [&] { return mPending.size() < sPendingMost; });
        mPending.push_back(Job{ .mPath = std::move(path), .mText = std::move(text), .mAs = as });
        mChanged.notify_all();
    }

    std::vector<std::string> FileWriter::finish()
    {
        std::unique_lock lock(mMutex);
        mChanged.wait(lock, [&] { return mPending.empty() && !mWriting; });
        std::vector<std::string> failed = std::move(mFailures);
        mFailures.clear();
        mFailures.reserve(sFailuresMost);
        return failed;
    }

    void FileWriter::serve(const std::stop_token& stop)
    {
        std::unique_lock lock(mMutex);
        // A stop asked while writes wait is answered once they are written: the wait ends on the stop only where
        // nothing waits.
        while (mChanged.wait(lock, stop, [&] { return !mPending.empty(); }))
        {
            const Job job = std::move(mPending.front());
            mPending.erase(mPending.begin());
            mWriting = true;
            lock.unlock();

            const Result<void, std::string> written
                = job.mAs == As::Whole ? writeReport(job.mPath, job.mText) : append(job.mPath, job.mText);

            lock.lock();
            mWriting = false;
            if (!written.isOk() && mFailures.size() < sFailuresMost)
                mFailures.push_back(written.error());
            mChanged.notify_all();
        }
    }
}
