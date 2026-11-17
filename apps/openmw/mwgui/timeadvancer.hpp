#pragma once

#include <cstdint>

#include <MyGUI_Delegate.h>

namespace MWGui
{
    class TimeAdvancer
    {
    public:
        TimeAdvancer();

        void run(std::int32_t hours, std::int32_t interruptAt = -1);
        void stop();
        void onFrame(float dt);

        std::int32_t getHours() const;
        bool isRunning() const;

        // signals
        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;
        typedef MyGUI::delegates::MultiDelegate<std::int32_t, std::int32_t> EventHandle_IntInt;

        EventHandle_IntInt eventProgressChanged;
        EventHandle_Void eventInterrupted;
        EventHandle_Void eventFinished;

    private:
        bool mRunning;

        std::int32_t mCurHour;
        std::int32_t mHours;
        std::int32_t mInterruptAt;

        float mRemainingTime;
    };
}
