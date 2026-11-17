#pragma once

#include <cstdint>
#include <map>
#include <string>

#include <QObject>
#include <QWidget>

class QCloseEvent;
class QLabel;
class QProgressBar;
class QDialogButtonBox;
class QListWidget;
class QVBoxLayout;

namespace CSMDoc
{
    class Document;
}

namespace CSVDoc
{
    class LoadingDocument : public QWidget
    {
        Q_OBJECT

        CSMDoc::Document* mDocument;
        QLabel* mTotalRecordsLabel;
        QLabel* mRecordsLabel;
        QProgressBar* mTotalProgress;
        QProgressBar* mRecordProgress;
        bool mAborted;
        QDialogButtonBox* mButtons;
        QLabel* mError;
        QListWidget* mMessages;
        QVBoxLayout* mLayout;
        std::int32_t mRecords;
        std::int32_t mTotalRecords;
        std::int32_t mFilesLoaded;

    private:
        void closeEvent(QCloseEvent* event) override;

    public:
        explicit LoadingDocument(CSMDoc::Document* document);

        void nextStage(const std::string& name, std::int32_t fileRecords);

        void nextRecord(std::int32_t records);

        void abort(const std::string& error);

        void addMessage(const std::string& message);

    private slots:

        void cancel();

    signals:

        void cancel(CSMDoc::Document* document);
        ///< Stop loading process.

        void close(CSMDoc::Document* document);
        ///< Close stopped loading process.
    };

    class Loader : public QObject
    {
        Q_OBJECT

        std::map<CSMDoc::Document*, LoadingDocument*> mDocuments;

    public:
        Loader() = default;

        ~Loader() override;

    signals:

        void cancel(CSMDoc::Document* document);

        void close(CSMDoc::Document* document);

    public slots:

        void add(CSMDoc::Document* document);

        void loadingStopped(CSMDoc::Document* document, bool completed, const std::string& error);

        void nextStage(CSMDoc::Document* document, const std::string& name, std::int32_t fileRecords);

        void nextRecord(CSMDoc::Document* document, std::int32_t records);

        void loadMessage(CSMDoc::Document* document, const std::string& message);
    };
}
