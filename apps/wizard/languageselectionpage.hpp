#pragma once

#include <QString>
#include <QWizard>

#include "ui_languageselectionpage.h"

class QObject;
class QWidget;

namespace Wizard
{
    class MainWizard;

    class LanguageSelectionPage : public QWizardPage, private Ui::LanguageSelectionPage
    {
        Q_OBJECT
    public:
        explicit LanguageSelectionPage(QWidget* parent);

        int nextId() const override;

    private:
        MainWizard* mWizard;
    };
}
