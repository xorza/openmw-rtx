#include "importpage.hpp"

#include <cstdint>

#include "mainwizard.hpp"

Wizard::ImportPage::ImportPage(QWidget* parent)
    : QWizardPage(parent)
{
    mWizard = qobject_cast<MainWizard*>(parent);

    setupUi(this);

    registerField(QStringLiteral("installation.import-settings"), importCheckBox);
    registerField(QStringLiteral("installation.import-addons"), addonsCheckBox);
    registerField(QStringLiteral("installation.import-fonts"), fontsCheckBox);
}

std::int32_t Wizard::ImportPage::nextId() const
{
    return MainWizard::Page_Conclusion;
}
