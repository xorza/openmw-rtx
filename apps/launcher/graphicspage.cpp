#include "graphicspage.hpp"

#include "sdlinit.hpp"

#include <components/misc/display.hpp>
#include <components/rtx/common/menu.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/sdlutil/sdldisplay.hpp>
#include <components/settings/values.hpp>

#include <QCoreApplication>
#include <QMessageBox>
#include <QScreen>
#include <QtGlobal>

#ifdef MAC_OS_X_VERSION_MIN_REQUIRED
#undef MAC_OS_X_VERSION_MIN_REQUIRED
// We need to do this because of Qt: https://bugreports.qt-project.org/browse/QTBUG-22154
#define MAC_OS_X_VERSION_MIN_REQUIRED __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__
#endif // MAC_OS_X_VERSION_MIN_REQUIRED

#include <SDL3/SDL_video.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace
{
    // In the context the .ui's own strings are translated in, which is where these came from
    constexpr std::array<Rtx::MenuLabel, Rtx::sUpscaleMenu.size()> sUpscaleLabels{ {
        { "off", QT_TRANSLATE_NOOP("GraphicsPage", "Off") },
        { "ultraperformance", QT_TRANSLATE_NOOP("GraphicsPage", "Ultra Performance") },
        { "performance", QT_TRANSLATE_NOOP("GraphicsPage", "Performance") },
        { "balanced", QT_TRANSLATE_NOOP("GraphicsPage", "Balanced") },
        { "quality", QT_TRANSLATE_NOOP("GraphicsPage", "Quality") },
        { "native", QT_TRANSLATE_NOOP("GraphicsPage", "Native") },
    } };
    static_assert(Rtx::followsMenu(sUpscaleLabels, Rtx::sUpscaleMenu));

    void addMenuItems(QComboBox* box, std::span<const Rtx::MenuLabel> labels)
    {
        for (const Rtx::MenuLabel& label : labels)
            box->addItem(QCoreApplication::translate("GraphicsPage", label.mLabel));
    }
}

Launcher::GraphicsPage::GraphicsPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("GraphicsPage");
    setupUi(this);

    // Set the maximum res we can set in windowed mode
    QRect res = getMaximumResolution();
    customWidthSpinBox->setMaximum(res.width());
    customHeightSpinBox->setMaximum(res.height());

    addMenuItems(rayTracingUpscaleComboBox, sUpscaleLabels);
    rayTracingDistantLandSpinBox->setRange(static_cast<int>(Settings::RTXCategory::sMinDistantLandCellsInMenu),
        static_cast<int>(Settings::RTXCategory::sMaxDistantLandCells));

    connect(windowModeComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
        &GraphicsPage::slotFullScreenChanged);
    connect(standardRadioButton, &QRadioButton::toggled, this, &GraphicsPage::slotStandardToggled);
    connect(screenComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &GraphicsPage::screenChanged);
    connect(framerateLimitCheckBox, &QCheckBox::toggled, this, &GraphicsPage::slotFramerateLimitToggled);
}

bool Launcher::GraphicsPage::setupSDL()
{
    bool sdlConnectSuccessful = initSDL();
    if (!sdlConnectSuccessful)
    {
        return false;
    }

    int displays = 0;
    SDL_free(SDL_GetDisplays(&displays));

    if (displays == 0)
    {
        QMessageBox msgBox;
        msgBox.setWindowTitle(tr("Error receiving number of screens"));
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setText(tr("<br><b>SDL_GetDisplays failed:</b><br><br>") + QString::fromUtf8(SDL_GetError()) + "<br>");
        msgBox.exec();
        return false;
    }

    screenComboBox->clear();
    mResolutionsPerScreen.clear();
    for (int i = 0; i < displays; i++)
    {
        mResolutionsPerScreen.append(getAvailableResolutions(i));
        screenComboBox->addItem(QString(tr("Screen ")) + QString::number(i + 1));
    }
    screenChanged(0);

    // Disconnect from SDL processes
    quitSDL();

    return true;
}

bool Launcher::GraphicsPage::loadSettings()
{
    if (!setupSDL())
        return false;

    // Visuals

    const int vsync = Settings::video().mVsyncMode;

    vSyncComboBox->setCurrentIndex(vsync);

    const Settings::WindowMode windowMode = Settings::video().mWindowMode;

    windowModeComboBox->setCurrentIndex(static_cast<int>(windowMode));
    handleWindowModeChange(windowMode);

    if (Settings::video().mWindowBorder)
        windowBorderCheckBox->setCheckState(Qt::Checked);

    if (Settings::rtx().mEnabled)
        rayTracingCheckBox->setCheckState(Qt::Checked);

    // Nothing selected where the setting names a mode the list does not offer, so saveSettings leaves it alone
    const std::optional<std::size_t> offered = Rtx::menuIndex(Rtx::sUpscaleMenu, Settings::rtx().mUpscale.get());
    rayTracingUpscaleComboBox->setCurrentIndex(offered ? static_cast<int>(*offered) : -1);

    // The box holds whole cells from the menu's fewest, so it shows nought or 4.5 as another value:
    // saveSettings writes the reach only when the player moved it
    rayTracingDistantLandSpinBox->setValue(static_cast<int>(std::lround(Settings::rtx().mDistantLandCells)));
    mLoadedDistantLandCells = rayTracingDistantLandSpinBox->value();

    // aaValue is the actual value (0, 1, 2, 4, 8, 16)
    const int aaValue = Settings::video().mAntialiasing;
    // aaIndex is the index into the allowed values in the pull down.
    const int aaIndex = antiAliasingComboBox->findText(QString::number(aaValue));
    if (aaIndex != -1)
        antiAliasingComboBox->setCurrentIndex(aaIndex);

    const int width = Settings::video().mResolutionX;
    const int height = Settings::video().mResolutionY;
    QString resolution = QString::number(width) + QString(" × ") + QString::number(height);
    screenComboBox->setCurrentIndex(Settings::video().mScreen);

    // Native is the list's first item, and a side of nought in the settings
    int resIndex = width == 0 || height == 0 ? 0 : resolutionComboBox->findText(resolution, Qt::MatchStartsWith);

    if (resIndex != -1)
    {
        standardRadioButton->toggle();
        resolutionComboBox->setCurrentIndex(resIndex);
    }
    else
    {
        customRadioButton->toggle();
        customWidthSpinBox->setValue(width);
        customHeightSpinBox->setValue(height);
    }

    const float fpsLimit = Settings::video().mFramerateLimit;
    if (fpsLimit != 0)
    {
        framerateLimitCheckBox->setCheckState(Qt::Checked);
        framerateLimitSpinBox->setValue(fpsLimit);
    }

    return true;
}

void Launcher::GraphicsPage::saveSettings()
{
    // Visuals

    Settings::video().mVsyncMode.set(static_cast<SDLUtil::VSyncMode>(vSyncComboBox->currentIndex()));
    Settings::video().mWindowMode.set(static_cast<Settings::WindowMode>(windowModeComboBox->currentIndex()));
    Settings::video().mWindowBorder.set(windowBorderCheckBox->checkState() == Qt::Checked);
    Settings::video().mAntialiasing.set(antiAliasingComboBox->currentText().toInt());

    Settings::rtx().mEnabled.set(rayTracingCheckBox->checkState() == Qt::Checked);
    // Nothing chosen leaves the setting alone, see loadSettings
    const int chosenIndex = rayTracingUpscaleComboBox->currentIndex();
    if (chosenIndex >= 0)
        if (const std::optional<std::string_view> chosen
            = Rtx::menuName(Rtx::sUpscaleMenu, static_cast<std::size_t>(chosenIndex)))
            Settings::rtx().mUpscale.set(std::string(*chosen));
    if (rayTracingDistantLandSpinBox->value() != mLoadedDistantLandCells)
        Settings::rtx().mDistantLandCells.set(static_cast<float>(rayTracingDistantLandSpinBox->value()));

    int cWidth = 0;
    int cHeight = 0;
    if (standardRadioButton->isChecked() && resolutionComboBox->currentIndex() > 0)
    {
        QRegularExpression resolutionRe("^(\\d+) × (\\d+)");
        QRegularExpressionMatch match = resolutionRe.match(resolutionComboBox->currentText().simplified());
        if (match.hasMatch())
        {
            cWidth = match.captured(1).toInt();
            cHeight = match.captured(2).toInt();
        }
    }
    else
    {
        cWidth = customWidthSpinBox->value();
        cHeight = customHeightSpinBox->value();
    }

    Settings::video().mResolutionX.set(cWidth);
    Settings::video().mResolutionY.set(cHeight);
    Settings::video().mScreen.set(screenComboBox->currentIndex());

    if (framerateLimitCheckBox->checkState() != Qt::Unchecked)
    {
        Settings::video().mFramerateLimit.set(framerateLimitSpinBox->value());
    }
    else if (Settings::video().mFramerateLimit != 0)
    {
        Settings::video().mFramerateLimit.set(0);
    }
}

QStringList Launcher::GraphicsPage::getAvailableResolutions(int screen)
{
    QStringList result;
    for (const SDLUtil::DisplayResolution& mode : SDLUtil::displayResolutions(SDLUtil::displayAt(screen)))
        result.append(QString::fromStdString(Misc::getResolutionText(mode.mWidth, mode.mHeight)));
    return result;
}

QRect Launcher::GraphicsPage::getMaximumResolution()
{
    QRect max;

    for (QScreen* screen : QGuiApplication::screens())
    {
        QRect res = screen->geometry();
        if (res.width() > max.width())
            max.setWidth(res.width() * screen->devicePixelRatio());
        if (res.height() > max.height())
            max.setHeight(res.height() * screen->devicePixelRatio());
    }
    return max;
}

void Launcher::GraphicsPage::screenChanged(int screen)
{
    if (screen >= 0)
    {
        resolutionComboBox->clear();
        resolutionComboBox->addItem(tr("Native"));
        resolutionComboBox->addItems(mResolutionsPerScreen[screen]);
    }
}

void Launcher::GraphicsPage::slotFullScreenChanged(int mode)
{
    handleWindowModeChange(static_cast<Settings::WindowMode>(mode));
}

void Launcher::GraphicsPage::handleWindowModeChange(Settings::WindowMode mode)
{
    // The resolution is the frame's in every mode, so the border alone depends on it
    const bool windowed = mode == Settings::WindowMode::Windowed;
    windowBorderCheckBox->setEnabled(windowed);
    windowBorderCheckBox->setToolTip(windowed ? QString() : tr("Window border is available only in Windowed mode."));
}

void Launcher::GraphicsPage::slotStandardToggled(bool checked)
{
    if (checked)
    {
        resolutionComboBox->setEnabled(true);
        customWidthSpinBox->setEnabled(false);
        customHeightSpinBox->setEnabled(false);
    }
    else
    {
        resolutionComboBox->setEnabled(false);
        customWidthSpinBox->setEnabled(true);
        customHeightSpinBox->setEnabled(true);
    }
}

void Launcher::GraphicsPage::slotFramerateLimitToggled(bool checked)
{
    framerateLimitSpinBox->setEnabled(checked);
}
