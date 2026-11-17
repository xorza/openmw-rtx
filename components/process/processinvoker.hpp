#pragma once

#include <cstdint>

#include <QProcess>
#include <QString>
#include <QStringList>

namespace Process
{
    class ProcessInvoker : public QObject
    {
        Q_OBJECT

    public:
        explicit ProcessInvoker(QObject* parent = nullptr);
        ~ProcessInvoker() override;

        //        void setProcessName(const QString &name);
        //        void setProcessArguments(const QStringList &arguments);

        QProcess* getProcess();
        //        QString getProcessName();
        //        QStringList getProcessArguments();

        //        inline bool startProcess(bool detached = false) { return startProcess(mName, mArguments, detached); }
        inline bool startProcess(const QString& name, bool detached = false)
        {
            return startProcess(name, QStringList(), detached);
        }
        bool startProcess(const QString& name, const QStringList& arguments, bool detached = false);

        void killProcess();

    private:
        QProcess* mProcess;

        QString mName;
        QStringList mArguments;

        bool mIgnoreErrors = false;

    private slots:
        void processError(QProcess::ProcessError error);
        void processFinished(std::int32_t exitCode, QProcess::ExitStatus exitStatus);
    };
}
