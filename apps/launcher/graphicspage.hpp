#pragma once

#include <memory>
#include <span>

#include <components/dlss/dlss.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/settings/windowmode.hpp>

#include "ui_graphicspage.h"

class QNetworkAccessManager;

namespace Files
{
    struct ConfigurationManager;
}

namespace Launcher
{
    class GraphicsSettings;

    class GraphicsPage : public QWidget, private Ui::GraphicsPage
    {
        Q_OBJECT

    public:
        /// @param dlss where the DLSS library is looked for and downloaded to, as the configuration
        ///        names the folders.
        explicit GraphicsPage(DlssLibrary::Folders dlss, QWidget* parent = nullptr);
        ~GraphicsPage() override;

        void saveSettings();
        bool loadSettings();

    public slots:
        void screenChanged(int screen);

    private slots:
        void slotFullScreenChanged(int mode);
        void slotStandardToggled(bool checked);
        void slotFramerateLimitToggled(bool checked);
        void downloadDlss();
        void readDlssDownload();
        void finishDlssDownload();

    private:
        struct DlssDownload;

        void buildUpscalerItems();
        void refreshDlssState();
        void setDlssItemEnabled(bool enabled);

        QVector<QStringList> mResolutionsPerScreen;

        /// What the upscaler box lists, a prefix of `Rtx::sUpscalerMenu`: DLSS where the build has
        /// it and the machine has an NVIDIA adapter.
        std::span<const Rtx::UpscalerChoice> mUpscalers;

        DlssLibrary::Folders mDlssFolders;
        QNetworkAccessManager* mNetwork = nullptr;

        /// The DLSS download under way, or nothing: one at a time.
        std::unique_ptr<DlssDownload> mDlssDownload;

        bool mDlssInstalled = false;
        int mLoadedDistantLandCells = 0;

        static QStringList getAvailableResolutions(int screen);
        static QRect getMaximumResolution();

        bool setupSDL();
        void handleWindowModeChange(Settings::WindowMode mode);
    };
}
