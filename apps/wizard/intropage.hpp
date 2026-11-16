#pragma once

#include <QWizard>

#include "ui_intropage.h"

namespace Wizard
{
    class MainWizard;

    class IntroPage : public QWizardPage, private Ui::IntroPage
    {
        Q_OBJECT
    public:
        explicit IntroPage(QWidget* parent);

        int nextId() const override;

    private:
        MainWizard* mWizard;
    };

}
