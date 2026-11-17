#include "methodselectionpage.hpp"

#include <cstdint>

#include <QDesktopServices>
#include <QUrl>

#include <components/misc/scalableicon.hpp>

#include "installsfromdisc.hpp"
#include "mainwizard.hpp"

Wizard::MethodSelectionPage::MethodSelectionPage(QWidget* parent)
    : QWizardPage(parent)
{
    mWizard = qobject_cast<MainWizard*>(parent);

    setupUi(this);

    installerIcon->setIcon(Misc::ScalableIcon::load(":system-installer"));
    folderIcon->setIcon(Misc::ScalableIcon::load(":folder"));
    buyLinkButton->setIcon(Misc::ScalableIcon::load(":dollar"));

    if constexpr (!sInstallsFromDisc)
    {
        retailDiscRadioButton->setEnabled(false);
        existingLocationRadioButton->setChecked(true);
    }

    QFont font = existingLocationRadioButton->font();
    font.setBold(true);
    existingLocationRadioButton->setFont(font);
    retailDiscRadioButton->setFont(font);

    registerField(QStringLiteral("installation.retailDisc"), retailDiscRadioButton);

    connect(buyLinkButton, &QPushButton::released, this, &MethodSelectionPage::handleBuyButton);
}

std::int32_t Wizard::MethodSelectionPage::nextId() const
{
    if (field(QStringLiteral("installation.retailDisc")).toBool())
        return MainWizard::Page_InstallationTarget;

    return MainWizard::Page_ExistingInstallation;
}

void Wizard::MethodSelectionPage::handleBuyButton()
{
    QDesktopServices::openUrl(QUrl("https://openmw.org/faq/#do_i_need_morrowind"));
}
