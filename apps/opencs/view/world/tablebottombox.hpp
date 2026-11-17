#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QWidget>

#include <apps/opencs/model/world/universalid.hpp>

#include "extendedcommandconfigurator.hpp"

class QLabel;
class QStackedLayout;
class QStatusBar;

namespace CSMDoc
{
    class Document;
}

namespace CSVWorld
{
    class CreatorFactoryBase;
    class Creator;

    class TableBottomBox : public QWidget
    {
        Q_OBJECT

        enum EditMode
        {
            EditMode_None,
            EditMode_Creation,
            EditMode_ExtendedConfig
        };

        bool mShowStatusBar;
        QLabel* mStatus;
        QStatusBar* mStatusBar;
        std::int32_t mStatusCount[4]{};

        EditMode mEditMode;
        Creator* mCreator;
        ExtendedCommandConfigurator* mExtendedConfigurator;

        QStackedLayout* mLayout;
        bool mHasPosition;
        std::int32_t mRow;
        std::int32_t mColumn;
        QString mStatusMessage;

    private:
        // not implemented
        TableBottomBox(const TableBottomBox&);
        TableBottomBox& operator=(const TableBottomBox&);

        void updateSize();

        void updateStatus();

        void extendedConfigRequest(ExtendedCommandConfigurator::Mode mode, const std::vector<std::string>& selectedIds);

    protected:
        bool event(QEvent* event) override;

    public:
        TableBottomBox(const CreatorFactoryBase& creatorFactory, CSMDoc::Document& document,
            const CSMWorld::UniversalId& id, QWidget* parent = nullptr);

        ~TableBottomBox() override;

        bool eventFilter(QObject* object, QEvent* event) override;

        void setEditLock(bool locked);

        void setStatusBar(bool show);

        bool canCreateAndDelete() const;
        ///< Is record creation and deletion supported?
        ///
        /// \note The BotomBox does not partake in the deletion of records.

        void setStatusMessage(const QString& message);

    signals:

        void requestFocus(const std::string& id);
        ///< Request owner of this box to focus the just created \a id. The owner may
        /// ignore this request.

    private slots:

        void requestDone();
        ///< \note This slot being called does not imply success.

        void currentWidgetChanged(std::int32_t index);

    public slots:

        void selectionSizeChanged(std::int32_t size);

        void tableSizeChanged(std::int32_t size, std::int32_t deleted, std::int32_t modified);
        ///< \param size Number of not deleted records
        /// \param deleted Number of deleted records
        /// \param modified Number of added and modified records

        void positionChanged(std::int32_t row, std::int32_t column);

        void noMorePosition();

        void createRequest();
        void createRecordsDirectlyRequest(const std::string& id);
        void cloneRequest(const std::string& id, const CSMWorld::UniversalId::Type type);
        void touchRequest(const std::vector<CSMWorld::UniversalId>&);

        void extendedDeleteConfigRequest(const std::vector<std::string>& selectedIds);
        void extendedRevertConfigRequest(const std::vector<std::string>& selectedIds);
    };
}
