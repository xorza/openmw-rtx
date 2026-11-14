#pragma once

#include <vector>

#include <QDockWidget>

class QVBoxLayout;

namespace CSVDoc
{
    class Operation;

    class Operations : public QDockWidget
    {
        Q_OBJECT

        QVBoxLayout* mLayout;
        std::vector<Operation*> mOperations;

    public:
        Operations();
        ~Operations() override = default;
        Operations(const Operations&) = delete;
        Operations& operator=(const Operations&) = delete;

        void setProgress(int current, int max, int type, int threads);
        ///< Implicitly starts the operation, if it is not running already.

        void quitOperation(int type);
        ///< Calling this function for an operation that is not running is a no-op.

    signals:

        void abortOperation(int type);
    };
}
