#ifndef GRAPHICSPAGE_H
#define GRAPHICSPAGE_H

#include "ui_graphicspage.h"

#include <memory>
#include <span>

#include <components/platform/dlss.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/settings/windowmode.hpp>

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
        explicit GraphicsPage(const Platform::Dlss::Folders& dlss, QWidget* parent = nullptr);
        ~GraphicsPage() override;

        void saveSettings();
        bool loadSettings();

    public slots:
        void screenChanged(int screen);

    private slots:
        void slotFullScreenChanged(int state);
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
        std::span<const Rtx::UpscalerKind> mUpscalers;

        Platform::Dlss::Folders mDlssFolders;
        QNetworkAccessManager* mNetwork = nullptr;

        /// The DLSS download under way, or nothing: one at a time.
        std::unique_ptr<DlssDownload> mDlssDownload;

        bool mDlssInstalled = false;
        int mLoadedDistantLandCells = 0;

        static QStringList getAvailableResolutions(int screen);
        static QRect getMaximumResolution();

        bool setupSDL();
        void handleWindowModeChange(Settings::WindowMode state);
    };
}
#endif
