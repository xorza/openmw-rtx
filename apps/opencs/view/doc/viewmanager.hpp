#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QObject>

#include "loader.hpp"

namespace CSMDoc
{
    class Document;
    class DocumentManager;
}

namespace CSVWorld
{
    class CommandDelegateFactoryCollection;
}

namespace CSMWorld
{
    class UniversalId;
}

namespace CSVDoc
{
    class View;

    class ViewManager : public QObject
    {
        Q_OBJECT

        CSMDoc::DocumentManager& mDocumentManager;
        std::vector<View*> mViews;
        CSVWorld::CommandDelegateFactoryCollection* mDelegateFactories;
        bool mExitOnSaveStateChange;
        bool mUserWarned;
        Loader mLoader;

        // not implemented
        ViewManager(const ViewManager&);
        ViewManager& operator=(const ViewManager&);

        void updateIndices();
        bool notifySaveOnClose(View* view = nullptr);
        bool showModifiedDocumentMessageBox(View* view);
        bool showSaveInProgressMessageBox(View* view);
        bool removeDocument(View* view);

    public:
        explicit ViewManager(CSMDoc::DocumentManager& documentManager);

        ~ViewManager() override;

        View* addView(CSMDoc::Document* document);
        ///< The ownership of the returned view is not transferred.

        View* addView(CSMDoc::Document* document, const CSMWorld::UniversalId& id, const std::string& hint);

        std::int32_t countViews(const CSMDoc::Document* document) const;
        ///< Return number of views for \a document.

        bool closeRequest(View* view);
        void removeDocAndView(CSMDoc::Document* document);

    signals:

        void newGameRequest();

        void newAddonRequest();

        void loadDocumentRequest();

        void closeMessageBox();

        void editSettingsRequest();

        void mergeDocument(CSMDoc::Document* document);

    public slots:

        void exitApplication(CSVDoc::View* view);

    private slots:

        void documentStateChanged(std::int32_t state, CSMDoc::Document* document);

        void progress(std::int32_t current, std::int32_t max, std::int32_t type, std::int32_t threads,
            CSMDoc::Document* document);

        void onExitWarningHandler(std::int32_t state, CSMDoc::Document* document);
    };

}
