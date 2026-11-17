#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <apps/opencs/model/world/universalid.hpp>
#include <apps/opencs/view/world/dragrecordtable.hpp>

class QAction;
class QSortFilterProxyModel;
class QContextMenuEvent;
class QMouseEvent;
class QObject;
class QWidget;

namespace CSMDoc
{
    class Document;
}

namespace CSMTools
{
    class ReportModel;
}

namespace CSMPrefs
{
    class Setting;
}

namespace CSVWorld
{
    class CommandDelegate;
}

namespace CSVTools
{
    class ReportTable : public CSVWorld::DragRecordTable
    {
        Q_OBJECT

        enum DoubleClickAction
        {
            Action_None,
            Action_Edit,
            Action_Remove,
            Action_EditAndRemove
        };

        QSortFilterProxyModel* mProxyModel;
        CSMTools::ReportModel* mModel;
        CSVWorld::CommandDelegate* mIdTypeDelegate;
        QAction* mShowAction;
        QAction* mRemoveAction;
        QAction* mReplaceAction;
        QAction* mRefreshAction;
        std::map<Qt::KeyboardModifiers, DoubleClickAction> mDoubleClickActions;
        std::int32_t mRefreshState;

    private:
        void contextMenuEvent(QContextMenuEvent* event) override;

        void mouseMoveEvent(QMouseEvent* event) override;

        void mouseDoubleClickEvent(QMouseEvent* event) override;

    public:
        /// \param richTextDescription Use rich text in the description column.
        /// \param refreshState Document state to check for refresh function. If value is
        /// 0 no refresh function exists. If the document current has the specified state
        /// the refresh function is disabled.
        ReportTable(CSMDoc::Document& document, const CSMWorld::UniversalId& id, bool richTextDescription,
            std::int32_t refreshState = 0, QWidget* parent = nullptr);

        std::vector<CSMWorld::UniversalId> getDraggedRecords() const override;

        void clear();

        /// Return indices of rows that are suitable for replacement.
        ///
        /// \param selection Only list selected rows.
        ///
        /// \return rows in the original model
        std::vector<std::int32_t> getReplaceIndices(bool selection) const;

        /// \return underlying report model
        CSMTools::ReportModel* getReportModel() const { return mModel; }

        /// \param index row in the original model
        void flagAsReplaced(std::int32_t index);

    private slots:

        void settingChanged(const CSMPrefs::Setting* setting);

        void showSelection();

        void removeSelection();

    public slots:

        void stateChanged(std::int32_t state, CSMDoc::Document* document);

    signals:

        void editRequest(const CSMWorld::UniversalId& id, const std::string& hint);

        void replaceRequest();

        void refreshRequest();
    };
}
