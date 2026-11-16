#pragma once

#include <QDialog>
#include <QMetaType>

#include <filesystem>

#include "pathmetatype.hpp"

class QPushButton;

namespace CSVDoc
{
    class FileWidget;
    class AdjusterWidget;

    class NewGameDialogue : public QDialog
    {
        Q_OBJECT

        QPushButton* mCreate;
        FileWidget* mFileWidget;
        AdjusterWidget* mAdjusterWidget;

    public:
        NewGameDialogue();

        void setLocalData(const std::filesystem::path& localData);

    signals:

        void createRequest(const std::filesystem::path& file);

        void cancelCreateGame();

    private slots:

        void stateChanged(bool valid);

        void create();

        void reject() override;
    };
}
