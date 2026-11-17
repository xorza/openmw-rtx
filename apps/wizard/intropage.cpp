#include "intropage.hpp"

#include <cstdint>

#include <QObject>
#include <QPixmap>
#include <QWidget>

#include "mainwizard.hpp"

Wizard::IntroPage::IntroPage(QWidget* parent)
    : QWizardPage(parent)
{
    mWizard = qobject_cast<MainWizard*>(parent);

    setupUi(this);
    setPixmap(QWizard::WatermarkPixmap, QPixmap(QStringLiteral(":/images/intropage-background.png")));
}

std::int32_t Wizard::IntroPage::nextId() const
{
    return MainWizard::Page_MethodSelection;
}
