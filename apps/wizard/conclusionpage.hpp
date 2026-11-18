#pragma once

#include <cstdint>

#include "ui_conclusionpage.h"

namespace Wizard
{
    class MainWizard;

    class ConclusionPage : public QWizardPage, private Ui::ConclusionPage
    {
        Q_OBJECT
    public:
        explicit ConclusionPage(QWidget* parent);

        std::int32_t nextId() const override;

    private:
        MainWizard* mWizard;

    protected:
        void initializePage() override;
    };

}
