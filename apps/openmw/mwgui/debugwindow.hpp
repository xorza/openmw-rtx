#pragma once

#include "windowbase.hpp"

namespace MWGui
{

    class DebugWindow : public WindowBase
    {
    public:
        DebugWindow();

        void onFrame(float dt) override;

        static void startLogRecording();

    private:
        void updateLogView();
        void updateLuaProfile();

        MyGUI::TabControl* mTabControl{};
        MyGUI::EditBox* mLogView;
        MyGUI::EditBox* mLuaProfiler;
    };

}
