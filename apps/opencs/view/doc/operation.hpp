#pragma once

#include <cstdint>

#include <QObject>

class QHBoxLayout;
class QPushButton;
class QProgressBar;
class QWidget;

namespace CSVDoc
{
    class Operation : public QObject
    {
        Q_OBJECT

        std::int32_t mType;
        bool mStalling;
        QProgressBar* mProgressBar{};
        QPushButton* mAbortButton{};
        QHBoxLayout* mLayout{};

        // not implemented
        Operation(const Operation&);
        Operation& operator=(const Operation&);

        void updateLabel(std::int32_t threads = -1);

    public:
        Operation(std::int32_t type, QWidget* parent);
        ~Operation() override;

        void setProgress(std::int32_t current, std::int32_t max, std::int32_t threads);

        std::int32_t getType() const;
        QHBoxLayout* getLayout() const;

    private:
        void setBarColor(std::int32_t type);
        void initWidgets();

    signals:

        void abortOperation(std::int32_t type);

    private slots:

        void abortOperation();
    };
}
