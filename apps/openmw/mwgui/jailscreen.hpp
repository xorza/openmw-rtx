#pragma once

#include <cstdint>

#include "timeadvancer.hpp"
#include "windowbase.hpp"

namespace MWGui
{
    class JailScreen : public WindowBase
    {
    public:
        JailScreen();
        void goToJail(std::int32_t days);

        void onFrame(float dt) override;

        bool exit() override { return false; }

        std::string_view getWindowIdForLua() const override { return "JailScreen"; }

    private:
        std::int32_t mDays;

        float mFadeTimeRemaining;

        MyGUI::ScrollBar* mProgressBar{};

        void onJailProgressChanged(std::int32_t cur, std::int32_t total);
        void onJailFinished();

        TimeAdvancer mTimeAdvancer;
    };
}
