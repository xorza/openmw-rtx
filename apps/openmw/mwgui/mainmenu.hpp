#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <thread>

#include "savegamedialog.hpp"
#include "windowbase.hpp"

namespace Gui
{
    class ImageButton;
}

namespace VFS
{
    class Manager;
}

namespace MWGui
{

    class BackgroundImage;
    class VideoWidget;
    class MenuVideo
    {
        MyGUI::ImageBox* mVideoBackground;
        VideoWidget* mVideo;
        std::thread mThread;
        bool mRunning;

        void run();

    public:
        explicit MenuVideo(const VFS::Manager* vfs);
        void resize(std::int32_t screenWidth, std::int32_t screenHeight);
        void commitFrame();
        ~MenuVideo();
    };

    class MainMenu : public WindowBase
    {
        std::int32_t mWidth;
        std::int32_t mHeight;

        bool mHasAnimatedMenu;

    public:
        MainMenu(std::int32_t w, std::int32_t h, const VFS::Manager* vfs, const std::string& versionDescription);

        void onResChange(std::int32_t w, std::int32_t h) override;
        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;

        void setVisible(bool visible) override;

        void onFrame(float) override;

        bool exit() override;

    private:
        const VFS::Manager* mVFS;

        MyGUI::Widget* mButtonBox;
        MyGUI::TextBox* mVersionText{};

        BackgroundImage* mBackground;

        std::optional<MenuVideo> mVideo; // For animated main menus

        std::map<std::string, Gui::ImageButton*, std::less<>> mButtons;

        void onButtonClicked(MyGUI::Widget* sender);
        void onNewGameConfirmed();
        void onExitConfirmed();

        void showBackground(bool show);

        void updateMenu();

        std::unique_ptr<SaveGameDialog> mSaveGameDialog;
    };

}
