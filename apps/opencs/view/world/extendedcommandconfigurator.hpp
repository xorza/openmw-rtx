#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include <QWidget>

#include <apps/opencs/model/world/universalid.hpp>

class QPushButton;
class QGroupBox;
class QCheckBox;
class QResizeEvent;

namespace CSMDoc
{
    class Document;
}

namespace CSMWorld
{
    class CommandDispatcher;
    class Data;
}

namespace CSVWorld
{
    class ExtendedCommandConfigurator : public QWidget
    {
        Q_OBJECT

    public:
        enum Mode
        {
            Mode_None,
            Mode_Delete,
            Mode_Revert
        };

    private:
        typedef std::map<QCheckBox*, CSMWorld::UniversalId> CheckBoxMap;

        QPushButton* mPerformButton;
        QPushButton* mCancelButton;
        QGroupBox* mTypeGroup;
        CheckBoxMap mTypeCheckBoxes;
        std::int32_t mNumUsedCheckBoxes;
        std::int32_t mNumChecked;

        Mode mMode;
        CSMWorld::CommandDispatcher* mCommandDispatcher;
        CSMWorld::Data& mData;
        std::vector<std::string> mSelectedIds;

        bool mEditLock;

        void setupGroupLayout();
        void setupCheckBoxes(const std::vector<CSMWorld::UniversalId>& types);
        void lockWidgets(bool locked);

    public:
        ExtendedCommandConfigurator(
            CSMDoc::Document& document, const CSMWorld::UniversalId& id, QWidget* parent = nullptr);

        void configure(Mode mode, const std::vector<std::string>& selectedIds);
        void setEditLock(bool locked);

    protected:
        void resizeEvent(QResizeEvent* event) override;

    private slots:
        void performExtendedCommand();
        void checkBoxStateChanged(std::int32_t state);
        void dataIdListChanged();

    signals:
        void done();
    };
}
