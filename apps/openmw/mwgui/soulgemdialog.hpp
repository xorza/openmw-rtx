#pragma once

#include <cstdint>

#include <apps/openmw/mwworld/ptr.hpp>

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

        void onButtonPressed(std::int32_t button);

    private:
        MessageBoxManager* mManager;
        MWWorld::Ptr mSoulgem;
    };

}
