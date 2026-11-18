#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <QObject>
#include <QProcess>
#include <QTextDocument>

#include <components/esm3/debugprofile.hpp>

class QTemporaryFile;

namespace CSMDoc
{
    class OperationHolder;

    class Runner : public QObject
    {
        Q_OBJECT

        QProcess mProcess;
        bool mRunning;
        ESM::DebugProfile mProfile;
        std::vector<std::filesystem::path> mContentFiles;
        std::string mStartupInstruction;
        QTemporaryFile* mStartup;
        QTextDocument mLog;
        std::filesystem::path mProjectPath;

    public:
        explicit Runner(std::filesystem::path projectPath);

        ~Runner() override;

        /// \param delayed Flag as running but do not start the OpenMW process yet (the
        /// process must be started by another call of start with delayed==false)
        void start(bool delayed = false);

        void stop();

        /// \note Running state is entered when the start function is called. This
        /// is not necessarily identical to the moment the child process is started.
        bool isRunning() const;

        void configure(const ESM::DebugProfile& profile, const std::vector<std::filesystem::path>& contentFiles,
            const std::string& startupInstruction);

        QTextDocument* getLog();

    signals:

        void runStateChanged();

    private slots:

        void finished(std::int32_t exitCode, QProcess::ExitStatus exitStatus);

        void readyReadStandardOutput();
    };

    /// \brief Watch for end of save operation and restart or stop runner
    class SaveWatcher : public QObject
    {
        Q_OBJECT

        Runner* mRunner;

    public:
        /// *this attaches itself to runner
        SaveWatcher(Runner* runner, OperationHolder* operation);

    private slots:

        void saveDone(std::int32_t type, bool failed);
    };
}
