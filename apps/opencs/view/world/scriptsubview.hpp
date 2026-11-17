#pragma once

#include <cstdint>
#include <string>

#include <QVBoxLayout>

#include <apps/opencs/model/world/commanddispatcher.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/doc/subview.hpp>

class QModelIndex;
class QObject;
class QSplitter;
class QTimer;

namespace CSMDoc
{
    class Document;
}

namespace CSMWorld
{
    class IdTable;
}

namespace CSMPrefs
{
    class Setting;
}

namespace CSVWorld
{
    class ScriptEdit;
    class RecordButtonBar;
    class TableBottomBox;
    class ScriptErrorTable;

    class ScriptSubView : public CSVDoc::SubView
    {
        Q_OBJECT

        ScriptEdit* mEditor;
        CSMDoc::Document& mDocument;
        CSMWorld::IdTable* mModel;
        std::int32_t mColumn;
        std::int32_t mIdColumn;
        std::int32_t mStateColumn;
        TableBottomBox* mBottom;
        RecordButtonBar* mButtons;
        CSMWorld::CommandDispatcher mCommandDispatcher;
        QVBoxLayout mLayout;
        QSplitter* mMain;
        ScriptErrorTable* mErrors;
        QTimer* mCompileDelay;
        std::int32_t mErrorHeight;

    private:
        void addButtonBar();

        void recompile();

        bool isDeleted() const;

        void updateDeletedState();

        void adjustSplitter();

    public:
        ScriptSubView(const CSMWorld::UniversalId& id, CSMDoc::Document& document);

        void setEditLock(bool locked) override;

        void useHint(const std::string& hint) override;

        void setStatusBar(bool show) override;

    public slots:

        void textChanged();

        void dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        void rowsAboutToBeRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end);

    private slots:

        void settingChanged(const CSMPrefs::Setting* setting);

        void updateStatusBar();

        void switchToRow(std::int32_t row);

        void switchToId(const std::string& id);

        void highlightError(std::int32_t line, std::int32_t column);

        void updateRequest();
    };
}
