#ifndef GRAPHICSPAGE_H
#define GRAPHICSPAGE_H

#include "ui_graphicspage.h"

#include <vector>

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
        explicit GraphicsPage(QWidget* parent = nullptr);

        void saveSettings();
        bool loadSettings();

    public slots:
        void screenChanged(int screen);

    private slots:
        void slotFullScreenChanged(int state);
        void slotStandardToggled(bool checked);
        void slotFramerateLimitToggled(bool checked);
        void downloadDlss();

    private:
        void buildUpscalerItems();
        void refreshDlssState();
        void setDlssItemEnabled(bool enabled);

        QVector<QStringList> mResolutionsPerScreen;
        std::vector<Rtx::UpscalerKind> mUpscalers;
        QNetworkAccessManager* mNetwork = nullptr;
        bool mDlssInstalled = false;
        int mLoadedDistantLandCells = 0;

        static QStringList getAvailableResolutions(int screen);
        static QRect getMaximumResolution();

        bool setupSDL();
        void handleWindowModeChange(Settings::WindowMode state);
    };
}
#endif
