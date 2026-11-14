#pragma once

#include "../mwworld/ptr.hpp"

namespace MWGui
{

    class MessageBoxManager;

    class SoulgemDialog
    {
    public:
        explicit SoulgemDialog(MessageBoxManager* manager)
            : mManager(manager)
        {
        }

        void show(const MWWorld::Ptr& soulgem);

        void onButtonPressed(int button);

    private:
        MessageBoxManager* mManager;
        MWWorld::Ptr mSoulgem;
    };

}
