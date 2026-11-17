#pragma once

#include <cstdint>

#include <QObject>
#include <QThread>

namespace CSMDoc
{
    class Operation;
    struct Message;

    class OperationHolder : public QObject
    {
        Q_OBJECT

        QThread mThread;
        Operation* mOperation;

    public:
        OperationHolder(QObject* parent, Operation* operation);

        ~OperationHolder() override;

        bool isRunning() const;

        void start();

        void abort();

        /// Stop the operation, wait for the thread to finish, and delete the operation.
        /// Safe to call multiple times.
        void quit();

    private slots:

        void doneSlot(std::int32_t type, bool failed);

    signals:

        void progress(std::int32_t current, std::int32_t max, std::int32_t type);

        void reportMessage(const CSMDoc::Message& message, std::int32_t type);

        void done(std::int32_t type, bool failed);

        void abortSignal();
    };
}
