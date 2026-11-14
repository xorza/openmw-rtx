#pragma once

#include "ui_methodselectionpage.h"

namespace Wizard
{
    class MainWizard;

    class MethodSelectionPage : public QWizardPage, private Ui::MethodSelectionPage
    {
        Q_OBJECT
    public:
        explicit MethodSelectionPage(QWidget* parent);

        int nextId() const override;

    private slots:
        void handleBuyButton();

    private:
        MainWizard* mWizard;
    };

}
