#pragma once

#include "ui_importpage.h"

namespace Wizard
{
    class MainWizard;

    class ImportPage : public QWizardPage, private Ui::ImportPage
    {
        Q_OBJECT
    public:
        explicit ImportPage(QWidget* parent);

        int nextId() const override;

    private:
        MainWizard* mWizard;
    };

}
