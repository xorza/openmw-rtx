#pragma once

#include <cstdint>

#include "ui_methodselectionpage.h"

namespace Wizard
{
    class MainWizard;

    class MethodSelectionPage : public QWizardPage, private Ui::MethodSelectionPage
    {
        Q_OBJECT
    public:
        explicit MethodSelectionPage(QWidget* parent);

        std::int32_t nextId() const override;

    private slots:
        void handleBuyButton();

    private:
        MainWizard* mWizard;
    };

}
