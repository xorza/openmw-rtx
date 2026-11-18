#pragma once

#include <cstdint>

#include "ui_componentselectionpage.h"

namespace Wizard
{
    class MainWizard;

    class ComponentSelectionPage : public QWizardPage, private Ui::ComponentSelectionPage
    {
        Q_OBJECT
    public:
        explicit ComponentSelectionPage(QWidget* parent);

        std::int32_t nextId() const override;
        bool validatePage() override;

    private slots:
        void updateButton();

    private:
        MainWizard* mWizard;

    protected:
        void initializePage() override;
    };

}
