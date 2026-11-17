#pragma once

#include <cstdint>
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

        void setProgress(std::int32_t current, std::int32_t max, std::int32_t type, std::int32_t threads);
        ///< Implicitly starts the operation, if it is not running already.

        void quitOperation(std::int32_t type);
        ///< Calling this function for an operation that is not running is a no-op.

    signals:

        void abortOperation(std::int32_t type);
    };
}
