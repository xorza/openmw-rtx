#include "graphicspage.hpp"

#include "sdlinit.hpp"

#include <components/files/qtconversion.hpp>
#include <components/misc/display.hpp>
#include <components/misc/presentation.hpp>
#include <components/platform/dlss.hpp>
#include <components/platform/gpu.hpp>
#include <components/rtx/common/menu.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/sdlutil/sdldisplay.hpp>
#include <components/settings/values.hpp>

#include <QCheckBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QStandardItemModel>
#include <QtGlobal>

#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    QString textOf(const std::string_view value)
    {
        return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
    }

    bool matches(const QByteArray& data, const std::string_view sha256)
    {
        return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()
            == QByteArray(sha256.data(), static_cast<int>(sha256.size()));
    }

    // In the context the .ui's own strings are translated in, which is where these came from
    constexpr std::array<Rtx::MenuLabel<Rtx::Upscale>, Rtx::sUpscaleMenu.size()> sUpscaleLabels{ {
        { Rtx::Upscale::Off, QT_TRANSLATE_NOOP("GraphicsPage", "Off") },
        { Rtx::Upscale::UltraPerformance, QT_TRANSLATE_NOOP("GraphicsPage", "Ultra Performance") },
        { Rtx::Upscale::Performance, QT_TRANSLATE_NOOP("GraphicsPage", "Performance") },
        { Rtx::Upscale::Balanced, QT_TRANSLATE_NOOP("GraphicsPage", "Balanced") },
        { Rtx::Upscale::Quality, QT_TRANSLATE_NOOP("GraphicsPage", "Quality") },
        { Rtx::Upscale::Native, QT_TRANSLATE_NOOP("GraphicsPage", "Native") },
    } };
    static_assert(Rtx::followsMenu(sUpscaleLabels, Rtx::sUpscaleMenu));

    constexpr std::array<Rtx::MenuLabel<Rtx::UpscalerKind>, Rtx::sUpscalerMenu.size()> sUpscalerLabels{ {
        { Rtx::UpscalerKind::Auto, QT_TRANSLATE_NOOP("GraphicsPage", "Auto") },
        { Rtx::UpscalerKind::Fsr, QT_TRANSLATE_NOOP("GraphicsPage", "AMD FSR 3.1") },
        { Rtx::UpscalerKind::Dlss, QT_TRANSLATE_NOOP("GraphicsPage", "NVIDIA DLSS") },
    } };
    static_assert(Rtx::followsMenu(sUpscalerLabels, Rtx::sUpscalerMenu));
    template <class Enum>
    void addMenuItems(QComboBox* box, std::span<const Rtx::MenuLabel<Enum>> labels)
    {
        for (const Rtx::MenuLabel<Enum>& label : labels)
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

    mNetwork = new QNetworkAccessManager(this);

    addMenuItems<Rtx::Upscale>(rayTracingUpscaleComboBox, sUpscaleLabels);
    buildUpscalerItems();
    rayTracingDistantLandSpinBox->setRange(static_cast<int>(Settings::RTXCategory::sMinDistantLandCellsInMenu),
        static_cast<int>(Settings::RTXCategory::sMaxDistantLandCells));

    connect(windowModeComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
        &GraphicsPage::slotFullScreenChanged);
    connect(standardRadioButton, &QRadioButton::toggled, this, &GraphicsPage::slotStandardToggled);
    connect(screenComboBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &GraphicsPage::screenChanged);
    connect(framerateLimitCheckBox, &QCheckBox::toggled, this, &GraphicsPage::slotFramerateLimitToggled);
    connect(downloadDlssButton, &QPushButton::clicked, this, &GraphicsPage::downloadDlss);
}

void Launcher::GraphicsPage::buildUpscalerItems()
{
    mUpscalers.clear();
    const bool nvidia = Platform::Gpu::has(Platform::Gpu::sNvidia);
    for (const Rtx::UpscalerKind kind : Rtx::sUpscalerMenu)
        if (kind != Rtx::UpscalerKind::Dlss || (Platform::Dlss::sBuilt && nvidia))
            mUpscalers.push_back(kind);

    std::vector<Rtx::MenuLabel<Rtx::UpscalerKind>> labels;
    labels.reserve(mUpscalers.size());
    for (const Rtx::UpscalerKind kind : mUpscalers)
        for (const Rtx::MenuLabel<Rtx::UpscalerKind>& label : sUpscalerLabels)
            if (label.mValue == kind)
            {
                labels.push_back(label);
                break;
            }

    rayTracingUpscalerComboBox->clear();
    addMenuItems<Rtx::UpscalerKind>(rayTracingUpscalerComboBox, labels);
    refreshDlssState();
}

void Launcher::GraphicsPage::refreshDlssState()
{
    const bool nvidia = Platform::Gpu::has(Platform::Gpu::sNvidia);
    mDlssInstalled = Platform::Dlss::installed();

    if (!Platform::Dlss::sBuilt || !nvidia)
    {
        downloadDlssButton->hide();
        dlssStatusLabel->hide();
        return;
    }

    setDlssItemEnabled(mDlssInstalled);
    downloadDlssButton->setVisible(!mDlssInstalled);
    dlssStatusLabel->setVisible(true);
    dlssStatusLabel->setText(mDlssInstalled
            ? tr("DLSS runtime installed.")
            : tr("DLSS is not installed. Download it from NVIDIA's GitHub to enable it; "
                 "<a href=\"%1\">NVIDIA's licence</a> applies.")
                  .arg(textOf(Platform::Dlss::sLicenseUrl)));
}

void Launcher::GraphicsPage::setDlssItemEnabled(const bool enabled)
{
    const auto found = std::find(mUpscalers.begin(), mUpscalers.end(), Rtx::UpscalerKind::Dlss);
    if (found == mUpscalers.end())
        return;

    if (auto* model = qobject_cast<QStandardItemModel*>(rayTracingUpscalerComboBox->model()))
        if (QStandardItem* item = model->item(static_cast<int>(std::distance(mUpscalers.begin(), found)));
            item != nullptr)
            item->setEnabled(enabled);
}

void Launcher::GraphicsPage::downloadDlss()
{
    if (mDlssInstalled)
        return;

    const Platform::Dlss::Download feature = Platform::Dlss::download();

    QNetworkRequest request(QUrl(textOf(feature.mUrl)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);

    QNetworkReply* reply = mNetwork->get(request);
    QProgressDialog progress(tr("Downloading NVIDIA DLSS..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    QObject::connect(
        reply, &QNetworkReply::downloadProgress, &progress, [&progress](const qint64 received, const qint64 total) {
            if (total > 0)
                progress.setValue(static_cast<int>(received * 100 / total));
        });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray downloaded = reply->readAll();
    const QNetworkReply::NetworkError failure = reply->error();
    const QString failureText = reply->errorString();
    reply->deleteLater();

    if (failure == QNetworkReply::OperationCanceledError)
        return;
    if (failure != QNetworkReply::NoError)
    {
        QMessageBox::critical(this, tr("Download NVIDIA DLSS"), tr("The download failed: %1").arg(failureText));
        return;
    }
    if (!matches(downloaded, feature.mSha256))
    {
        QMessageBox::critical(
            this, tr("Download NVIDIA DLSS"), tr("The downloaded file did not match NVIDIA's pinned checksum."));
        return;
    }

    const std::filesystem::path directory = Platform::Dlss::runtimeDirectory();
    if (directory.empty())
    {
        QMessageBox::critical(
            this, tr("Download NVIDIA DLSS"), tr("OpenMW could not find a folder to put the DLSS runtime in."));
        return;
    }

    std::error_code errorCode;
    std::filesystem::create_directories(directory, errorCode);
    if (errorCode)
    {
        QMessageBox::critical(this, tr("Download NVIDIA DLSS"),
            tr("The DLSS folder could not be created: %1").arg(QString::fromStdString(errorCode.message())));
        return;
    }

    const std::filesystem::path target = directory / feature.mFileName;
    QSaveFile file(Files::pathToQString(target));
    if (!file.open(QIODevice::WriteOnly) || file.write(downloaded) != downloaded.size() || !file.commit())
    {
        QMessageBox::critical(
            this, tr("Download NVIDIA DLSS"), tr("The DLSS runtime could not be written: %1").arg(file.errorString()));
        return;
    }
    QFile::setPermissions(Files::pathToQString(target),
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);

    mDlssInstalled = true;
    setDlssItemEnabled(true);
    downloadDlssButton->hide();
    dlssStatusLabel->setText(tr("DLSS runtime installed."));

    const auto found = std::find(mUpscalers.begin(), mUpscalers.end(), Rtx::UpscalerKind::Dlss);
    if (found != mUpscalers.end())
        rayTracingUpscalerComboBox->setCurrentIndex(static_cast<int>(std::distance(mUpscalers.begin(), found)));
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

    // The list offers every mode the setting can hold
    rayTracingUpscaleComboBox->setCurrentIndex(
        static_cast<int>(Rtx::menuIndex(Rtx::sUpscaleMenu, Settings::rtx().mUpscale.get()).value()));

    refreshDlssState();
    const auto upscaler = std::find(
        mUpscalers.begin(), mUpscalers.end(), Rtx::launcherUpscaler(Settings::rtx().mUpscaler.get(), mDlssInstalled));
    rayTracingUpscalerComboBox->setCurrentIndex(
        upscaler != mUpscalers.end() ? static_cast<int>(std::distance(mUpscalers.begin(), upscaler)) : 0);

    // The box holds whole cells from the menu's fewest, so it shows nought or 4.5 as another value:
    // saveSettings writes the reach only when the player moved it
    rayTracingDistantLandSpinBox->setValue(static_cast<int>(std::lround(Settings::rtx().mDistantLandCells)));
    mLoadedDistantLandCells = rayTracingDistantLandSpinBox->value();

    const int width = Settings::video().mResolutionX;
    const int height = Settings::video().mResolutionY;
    QString resolution = QString::number(width) + QString(" × ") + QString::number(height);
    screenComboBox->setCurrentIndex(Settings::video().mScreen);

    // Native is the list's first item, and a side of nought in the settings
    const int listed = resolutionComboBox->findText(resolution, Qt::MatchStartsWith);
    switch (Misc::resolutionPickOf(osg::Vec2i(width, height), listed != -1))
    {
        case Misc::ResolutionPick::Native:
            standardRadioButton->toggle();
            resolutionComboBox->setCurrentIndex(0);
            break;
        case Misc::ResolutionPick::Listed:
            standardRadioButton->toggle();
            resolutionComboBox->setCurrentIndex(listed);
            break;
        case Misc::ResolutionPick::Custom:
            customRadioButton->toggle();
            customWidthSpinBox->setValue(width);
            customHeightSpinBox->setValue(height);
            break;
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

    // Nothing chosen leaves the setting alone
    const int chosenIndex = rayTracingUpscaleComboBox->currentIndex();
    if (chosenIndex >= 0)
        if (const std::optional<Rtx::Upscale> chosen
            = Rtx::menuValue(Rtx::sUpscaleMenu, static_cast<std::size_t>(chosenIndex)))
            Settings::rtx().mUpscale.set(*chosen);
    if (const int chosenUpscaler = rayTracingUpscalerComboBox->currentIndex();
        chosenUpscaler >= 0 && chosenUpscaler < static_cast<int>(mUpscalers.size()))
    {
        // A disabled entry can still be the model's current row after the library was removed;
        // writing `dlss` there would leave a configuration the game must refuse to start.
        Settings::rtx().mUpscaler.set(
            Rtx::launcherUpscaler(mUpscalers[static_cast<std::size_t>(chosenUpscaler)], mDlssInstalled));
    }
    if (rayTracingDistantLandSpinBox->value() != mLoadedDistantLandCells)
        Settings::rtx().mDistantLandCells.set(static_cast<float>(rayTracingDistantLandSpinBox->value()));

    // Native is the list's first item: what was typed into the custom sides is not what it saves.
    const Misc::ResolutionPick pick = customRadioButton->isChecked() ? Misc::ResolutionPick::Custom
        : resolutionComboBox->currentIndex() > 0                     ? Misc::ResolutionPick::Listed
                                                                     : Misc::ResolutionPick::Native;
    const osg::Vec2i picked = Misc::resolutionPicked(pick, resolutionComboBox->currentText().simplified().toStdString(),
        osg::Vec2i(customWidthSpinBox->value(), customHeightSpinBox->value()));

    Settings::video().mResolutionX.set(picked.x());
    Settings::video().mResolutionY.set(picked.y());
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
