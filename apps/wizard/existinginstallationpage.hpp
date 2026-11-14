#pragma once

#include "ui_existinginstallationpage.h"

namespace Wizard
{
    class MainWizard;

    class ExistingInstallationPage : public QWizardPage, private Ui::ExistingInstallationPage
    {
        Q_OBJECT
    public:
        explicit ExistingInstallationPage(QWidget* parent);

        int nextId() const override;
        bool isComplete() const override;
        bool validatePage() override;

    private slots:
        void browseButtonClicked();
        void textChanged(const QString& text);

    private:
        MainWizard* mWizard;

    protected:
        void initializePage() override;
    };

}
