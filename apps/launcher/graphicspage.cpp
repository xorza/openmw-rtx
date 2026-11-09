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
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

namespace
{
    QString textOf(const std::string_view value)
    {
        return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
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

Launcher::GraphicsPage::GraphicsPage(const Platform::Dlss::Folders& dlss, QWidget* parent)
    : QWidget(parent)
    , mDlssFolders(dlss)
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

/// What one download of the DLSS library holds while it runs: the bytes go into the file and the
/// hash as they arrive, so memory holds a chunk and not the library, and the file is committed only
/// once the size and the checksum match NVIDIA's pinned release.
struct Launcher::GraphicsPage::DlssDownload
{
    enum class Failure
    {
        None,
        TooLarge,
        NotWritten,
    };

    DlssDownload(const Platform::Dlss::Download& feature, const QString& target)
        : mFeature(feature)
        , mFile(target)
    {
    }

    /// Takes what the reply has received into the file and the hash, and aborts the reply where it
    /// passes the pinned size or the file refuses it.
    void take()
    {
        const QByteArray chunk = mReply->readAll();
        mReceived += static_cast<std::uint64_t>(chunk.size());

        if (mReceived > mFeature.mBytes)
            mFailure = Failure::TooLarge;
        else if (mFile.write(chunk) != chunk.size())
            mFailure = Failure::NotWritten;
        else
        {
            mHash.addData(chunk);
            return;
        }

        // Aborting ends the reply, whose `finished` may let go of this before `abort` returns:
        // nothing of this is read after it.
        mReply->abort();
    }

    Platform::Dlss::Download mFeature;
    QSaveFile mFile;
    QCryptographicHash mHash{ QCryptographicHash::Sha256 };
    std::uint64_t mReceived = 0;
    Failure mFailure = Failure::None;

    /// Owned by the page's network manager and the page, and let go of with `deleteLater` once the
    /// download ends, since either may be the sender of the signal that ends it.
    QNetworkReply* mReply = nullptr;
    QProgressDialog* mProgress = nullptr;
};

Launcher::GraphicsPage::~GraphicsPage() = default;

void Launcher::GraphicsPage::buildUpscalerItems()
{
    mUpscalers = Rtx::offeredUpscalers(Platform::Dlss::sBuilt && Platform::Gpu::has(Platform::Gpu::sNvidia));
    addMenuItems<Rtx::UpscalerKind>(rayTracingUpscalerComboBox, std::span(sUpscalerLabels).first(mUpscalers.size()));
    refreshDlssState();
}

void Launcher::GraphicsPage::refreshDlssState()
{
    mDlssInstalled = Platform::Dlss::installedIn(mDlssFolders);

    if (std::ranges::find(mUpscalers, Rtx::UpscalerKind::Dlss) == mUpscalers.end())
    {
        downloadDlssButton->hide();
        dlssStatusLabel->hide();
        return;
    }

    setDlssItemEnabled(mDlssInstalled);
    downloadDlssButton->setVisible(!mDlssInstalled);
    dlssStatusLabel->setVisible(true);
    dlssStatusLabel->setText(mDlssInstalled
            ? tr("DLSS is installed.")
            : tr("DLSS is not installed. <a href=\"%1\">NVIDIA's licence</a> applies to the download.")
                  .arg(textOf(Platform::Dlss::sLicenseUrl)));
}

void Launcher::GraphicsPage::setDlssItemEnabled(const bool enabled)
{
    const auto found = std::ranges::find(mUpscalers, Rtx::UpscalerKind::Dlss);
    if (found == mUpscalers.end())
        return;

    if (auto* model = qobject_cast<QStandardItemModel*>(rayTracingUpscalerComboBox->model()))
        if (QStandardItem* item = model->item(static_cast<int>(std::distance(mUpscalers.begin(), found)));
            item != nullptr)
            item->setEnabled(enabled);
}

void Launcher::GraphicsPage::downloadDlss()
{
    if (mDlssInstalled || mDlssDownload != nullptr)
        return;

    const std::filesystem::path& directory = mDlssFolders.mRuntime;
    if (directory.empty())
    {
        QMessageBox::critical(this, tr("Download DLSS"), tr("Could not find a folder for DLSS."));
        return;
    }

    std::error_code errorCode;
    std::filesystem::create_directories(directory, errorCode);
    if (errorCode)
    {
        QMessageBox::critical(this, tr("Download DLSS"),
            tr("Could not create the DLSS folder: %1").arg(QString::fromStdString(errorCode.message())));
        return;
    }

    const Platform::Dlss::Download feature = Platform::Dlss::download();
    auto download = std::make_unique<DlssDownload>(feature, Files::pathToQString(directory / feature.mFileName));
    if (!download->mFile.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(
            this, tr("Download DLSS"), tr("Could not save DLSS: %1").arg(download->mFile.errorString()));
        return;
    }

    QNetworkRequest request(QUrl(textOf(feature.mUrl)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    download->mReply = mNetwork->get(request);

    download->mProgress = new QProgressDialog(tr("Downloading DLSS..."), tr("Cancel"), 0, 100, this);
    download->mProgress->setWindowModality(Qt::WindowModal);
    download->mProgress->setAutoReset(false);

    QProgressDialog* const progress = download->mProgress;
    const std::uint64_t pinned = feature.mBytes;
    connect(download->mReply, &QNetworkReply::downloadProgress, progress, [progress, pinned](const qint64 received) {
        progress->setValue(static_cast<int>(std::min<std::uint64_t>(received, pinned) * 100 / pinned));
    });
    connect(progress, &QProgressDialog::canceled, download->mReply, &QNetworkReply::abort);
    connect(download->mReply, &QNetworkReply::readyRead, this, &GraphicsPage::readDlssDownload);
    connect(download->mReply, &QNetworkReply::finished, this, &GraphicsPage::finishDlssDownload);

    downloadDlssButton->setEnabled(false);
    mDlssDownload = std::move(download);
    progress->open();
}

void Launcher::GraphicsPage::readDlssDownload()
{
    if (mDlssDownload != nullptr && mDlssDownload->mFailure == DlssDownload::Failure::None)
        mDlssDownload->take();
}

void Launcher::GraphicsPage::finishDlssDownload()
{
    if (mDlssDownload == nullptr)
        return;

    // The last bytes, which may come with the end and no `readyRead` of their own.
    if (mDlssDownload->mFailure == DlssDownload::Failure::None
        && mDlssDownload->mReply->error() == QNetworkReply::NoError)
        mDlssDownload->take();

    const std::unique_ptr<DlssDownload> download = std::move(mDlssDownload);
    download->mReply->deleteLater();
    // Hidden and not closed: a progress dialog that closes says it was cancelled.
    download->mProgress->hide();
    download->mProgress->deleteLater();
    downloadDlssButton->setEnabled(true);

    const QString title = tr("Download DLSS");
    switch (download->mFailure)
    {
        case DlssDownload::Failure::TooLarge:
            QMessageBox::critical(this, title, tr("The downloaded file is larger than expected."));
            return;
        case DlssDownload::Failure::NotWritten:
            QMessageBox::critical(this, title, tr("Could not save DLSS: %1").arg(download->mFile.errorString()));
            return;
        case DlssDownload::Failure::None:
            break;
    }

    const QNetworkReply::NetworkError failure = download->mReply->error();
    if (failure == QNetworkReply::OperationCanceledError)
        return;
    if (failure != QNetworkReply::NoError)
    {
        QMessageBox::critical(this, title, tr("The download failed: %1").arg(download->mReply->errorString()));
        return;
    }

    const std::string_view sha256 = download->mFeature.mSha256;
    if (download->mReceived != download->mFeature.mBytes
        || download->mHash.result().toHex() != QByteArray(sha256.data(), static_cast<qsizetype>(sha256.size())))
    {
        QMessageBox::critical(this, title, tr("The downloaded file has the wrong checksum."));
        return;
    }

    if (!download->mFile.commit())
    {
        QMessageBox::critical(this, title, tr("Could not save DLSS: %1").arg(download->mFile.errorString()));
        return;
    }

    mDlssInstalled = true;
    setDlssItemEnabled(true);
    downloadDlssButton->hide();
    dlssStatusLabel->setText(tr("DLSS is installed."));

    const auto found = std::ranges::find(mUpscalers, Rtx::UpscalerKind::Dlss);
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

    // A kind the box does not list shows nothing chosen, and saving leaves the setting alone.
    const auto upscaler
        = std::ranges::find(mUpscalers, Rtx::launcherUpscaler(Settings::rtx().mUpscaler.get(), mDlssInstalled));
    rayTracingUpscalerComboBox->setCurrentIndex(
        upscaler != mUpscalers.end() ? static_cast<int>(std::distance(mUpscalers.begin(), upscaler)) : -1);

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
