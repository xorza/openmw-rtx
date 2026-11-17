#pragma once

#include <cstdint>

#include <QWizard>

#include "ui_installationtargetpage.h"

namespace Files
{
    struct ConfigurationManager;
}

namespace Wizard
{
    class MainWizard;

    class InstallationTargetPage : public QWizardPage, private Ui::InstallationTargetPage
    {
        Q_OBJECT
    public:
        InstallationTargetPage(QWidget* parent, const Files::ConfigurationManager& cfg);

        std::int32_t nextId() const override;
        bool validatePage() override;

    private slots:
        void browseButtonClicked();

    private:
        const Files::ConfigurationManager& mCfgMgr;

    protected:
        void initializePage() override;
    };

}
