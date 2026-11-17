#pragma once

#include <cstdint>

#include "ui_importpage.h"

namespace Wizard
{
    class MainWizard;

    class ImportPage : public QWizardPage, private Ui::ImportPage
    {
        Q_OBJECT
    public:
        explicit ImportPage(QWidget* parent);

        std::int32_t nextId() const override;

    private:
        MainWizard* mWizard;
    };

}
