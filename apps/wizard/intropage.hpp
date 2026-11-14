#pragma once

#include "ui_intropage.h"

#include <QWizard>

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
