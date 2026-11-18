#pragma once

#include <cstddef>
#include <cstdint>

#include <MyGUI_ListBox.h>

#include <components/files/configurationmanager.hpp>
#include <components/settings/shadermanager.hpp>
#include <components/vfs/pathutil.hpp>

#include "windowbase.hpp"

namespace MyGUI
{
    class ScrollView;
    class EditBox;
    class TabItem;
}
namespace Gui
{
    class AutoSizedButton;
    class AutoSizedEditBox;
}

namespace MWGui
{
    class PostProcessorHud : public WindowBase
    {
        class ListWrapper final : public MyGUI::ListBox
        {
            MYGUI_RTTI_DERIVED(ListWrapper)
        protected:
            void onKeyButtonPressed(MyGUI::KeyCode key, MyGUI::Char ch) override;
        };

    public:
        explicit PostProcessorHud(Files::ConfigurationManager& cfgMgr);

        void onOpen() override;

        void onClose() override;

        void onResChange(std::int32_t width, std::int32_t height) override;

        void updateTechniques();

        void toggleMode(Settings::ShaderManager::Mode mode);

        static void registerMyGUIComponents();

    private:
        void notifyWindowResize(MyGUI::Window* sender);

        void notifyFilterChanged(MyGUI::EditBox* sender);

        void updateConfigView(VFS::Path::NormalizedView path);

        void notifyResetButtonClicked(MyGUI::Widget* sender);

        void notifyListChangePosition(MyGUI::ListBox* sender, std::size_t index);

        void notifyKeyButtonPressed(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char ch);

        void notifyActivatePressed(MyGUI::Widget* sender);

        void notifyDeactivatePressed(MyGUI::Widget* sender);

        void notifyShaderUpPressed(MyGUI::Widget* sender);

        void notifyShaderDownPressed(MyGUI::Widget* sender);

        void notifyMouseWheel(MyGUI::Widget* sender, std::int32_t rel);

        enum class Direction
        {
            Up,
            Down
        };

        void moveShader(Direction direction);

        void toggleTechnique(bool enabled);

        void select(ListWrapper* list, std::size_t index);

        void layout();

        ListWrapper* mActiveList{};
        ListWrapper* mInactiveList{};

        Gui::AutoSizedButton* mButtonActivate{};
        Gui::AutoSizedButton* mButtonDeactivate{};
        Gui::AutoSizedButton* mButtonDown{};
        Gui::AutoSizedButton* mButtonUp{};

        MyGUI::ScrollView* mConfigLayout{};

        MyGUI::Widget* mConfigArea;

        MyGUI::EditBox* mFilter{};
        Gui::AutoSizedEditBox* mShaderInfo;

        std::string mOverrideHint;

        std::int32_t mOffset = 0;

        Files::ConfigurationManager& mCfgMgr;
    };
}
