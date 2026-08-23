#ifndef MWINPUT_ACTIONMANAGER_H
#define MWINPUT_ACTIONMANAGER_H

namespace MWRender
{
    class Stage;
}

namespace MWInput
{
    class BindingsManager;

    class ActionManager
    {
    public:
        ActionManager(BindingsManager* bindingsManager, MWRender::Stage& stage);

        void update(float dt);

        void executeAction(int action);

        bool checkAllowedToUseItems() const;

        void toggleMainMenu();
        void toggleConsole();
        void screenshot();
        void activate();
        void rest();
        void quickLoad();
        void quickSave();

        void quickKey(int index);

        void resetIdleTime();
        float getIdleTime() const { return mTimeIdle; }

        bool isSneaking() const;

    private:
        void handleGuiArrowKey(int action);

        BindingsManager* mBindingsManager;
        MWRender::Stage& mStage;

        float mTimeIdle;
    };
}
#endif
