#pragma once

#include <cstdint>

namespace MWRender
{
    class Renderer;
}

namespace MWInput
{
    class BindingsManager;

    class ActionManager
    {
    public:
        ActionManager(BindingsManager* bindingsManager, MWRender::Renderer& renderer);

        void update(float dt);

        void executeAction(std::int32_t action);

        bool checkAllowedToUseItems() const;

        void toggleMainMenu();
        void toggleConsole();
        void screenshot();
        void activate();
        void rest();
        void quickLoad();
        void quickSave();

        void quickKey(std::int32_t index);

        void resetIdleTime();
        float getIdleTime() const { return mTimeIdle; }

        bool isSneaking() const;

    private:
        void handleGuiArrowKey(std::int32_t action);

        BindingsManager* mBindingsManager;
        MWRender::Renderer& mRenderer;

        float mTimeIdle;
    };
}
