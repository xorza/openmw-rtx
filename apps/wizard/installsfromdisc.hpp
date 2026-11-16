#pragma once

class QWidget;
class QWizardPage;

namespace Config
{
    class GameSettings;
}

namespace Wizard
{
    /// Whether the wizard unpacks Morrowind from its installer's archives (libunshield), as it does on every system but
    /// Windows, whose players run the installer itself.
    inline constexpr bool sInstallsFromDisc = OPENMW_USE_UNSHIELD;

    /// The page that unpacks the archives: defined only where `sInstallsFromDisc` holds, and named only in a branch
    /// that holds it, which a build without it discards.
    QWizardPage* makeInstallationPage(QWidget* parent, Config::GameSettings& gameSettings);
}
