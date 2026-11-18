#include "loader.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>

#include <QTimer>

#include <apps/opencs/model/tools/reportmodel.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>

#include "document.hpp"
#include "messages.hpp"

CSMDoc::Loader::Loader()
    : mShouldStop(false)
{
    mTimer = new QTimer(this);

    connect(mTimer, &QTimer::timeout, this, &Loader::load);
    mTimer->start();
}

QWaitCondition& CSMDoc::Loader::hasThingsToDo()
{
    return mThingsToDo;
}

void CSMDoc::Loader::stop()
{
    mShouldStop = true;
}

void CSMDoc::Loader::load()
{
    if (mDocuments.empty())
    {
        mMutex.lock();
        mThingsToDo.wait(&mMutex);
        mMutex.unlock();

        if (mShouldStop)
            mTimer->stop();

        return;
    }

    if (!mStart.has_value())
        mStart = std::chrono::steady_clock::now();

    auto iter = mDocuments.begin();

    Document* document = iter->first;

    auto size = static_cast<std::int32_t>(document->getContentFiles().size());
    std::int32_t editedIndex = size - 1; // index of the file to be edited/created

    if (document->isNew())
        --size;

    bool done = false;

    try
    {
        if (iter->second.mRecordsLeft)
        {
            Messages messages(Message::Severity_Error);
            const std::int32_t batchingSize = 50;
            for (std::int32_t i = 0; i < batchingSize; ++i) // do not flood the system with update signals
                if (document->getData().continueLoading(messages))
                {
                    iter->second.mRecordsLeft = false;
                    break;
                }
                else
                    ++(iter->second.mRecordsLoaded);

            CSMWorld::UniversalId log(CSMWorld::UniversalId::Type_LoadErrorLog, 0);

            { // silence a g++ warning
                for (const auto& message : messages)
                {
                    document->getReport(log)->add(message);
                    emit loadMessage(document, message.mMessage);
                }
            }

            emit nextRecord(document, iter->second.mRecordsLoaded);

            return;
        }

        if (iter->second.mFile < size) // start loading the files
        {
            const std::filesystem::path& path
                = document->getContentFiles()[static_cast<std::size_t>(iter->second.mFile)];

            std::int32_t steps
                = document->getData().startLoading(path, iter->second.mFile != editedIndex, /*project*/ false);
            iter->second.mRecordsLeft = true;
            iter->second.mRecordsLoaded = 0;

            emit nextStage(document, Files::pathToUnicodeString(path.filename()), steps);
        }
        else if (iter->second.mFile == size) // start loading the last (project) file
        {
            std::int32_t steps = document->getData().startLoading(document->getProjectPath(), /*base*/ false, true);
            iter->second.mRecordsLeft = true;
            iter->second.mRecordsLoaded = 0;

            emit nextStage(document, "Project File", steps);
        }
        else
        {
            document->getData().finishLoading();
            done = true;
        }

        ++(iter->second.mFile);
    }
    catch (const std::exception& e)
    {
        mDocuments.erase(iter);
        emit documentNotLoaded(document, e.what());
        return;
    }

    if (done)
    {
        if (mStart.has_value())
        {
            const auto duration = std::chrono::steady_clock::now() - *mStart;
            Log(Debug::Verbose) << "Loaded content files in "
                                << std::chrono::duration_cast<std::chrono::duration<double>>(duration).count() << 's';
            mStart.reset();
        }

        mDocuments.erase(iter);
        emit documentLoaded(document);
    }
}

void CSMDoc::Loader::loadDocument(CSMDoc::Document* document)
{
    mDocuments.emplace_back(document, Stage());
}

void CSMDoc::Loader::abortLoading(CSMDoc::Document* document)
{
    for (auto iter = mDocuments.begin(); iter != mDocuments.end(); ++iter)
    {
        if (iter->first == document)
        {
            mDocuments.erase(iter);
            emit documentNotLoaded(document, "");
            break;
        }
    }
}
