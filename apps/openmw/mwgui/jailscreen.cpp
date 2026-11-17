#include "jailscreen.hpp"

#include <cstdint>

#include <MyGUI_Delegate.h>
#include <MyGUI_ScrollBar.h>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/luamanager.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwworld/class.hpp>

#include "mode.hpp"

namespace MWGui
{
    JailScreen::JailScreen()
        : WindowBase("openmw_jail_screen.layout")
        , mDays(1)
        , mFadeTimeRemaining(0)
    {
        getWidget(mProgressBar, "ProgressBar");

        mTimeAdvancer.eventProgressChanged += MyGUI::newDelegate(this, &JailScreen::onJailProgressChanged);
        mTimeAdvancer.eventFinished += MyGUI::newDelegate(this, &JailScreen::onJailFinished);

        center();
    }

    void JailScreen::goToJail(std::int32_t days)
    {
        mDays = days;

        MWBase::Environment::get().getWindowManager()->fadeScreenOut(0.5);
        mFadeTimeRemaining = 0.5;

        setVisible(false);
        mProgressBar->setScrollRange(100 + 1);
        mProgressBar->setScrollPosition(0);
        mProgressBar->setTrackSize(0);
    }

    void JailScreen::onFrame(float dt)
    {
        mTimeAdvancer.onFrame(dt);

        if (mFadeTimeRemaining <= 0)
            return;

        mFadeTimeRemaining -= dt;

        if (mFadeTimeRemaining <= 0)
        {
            MWWorld::Ptr player = MWMechanics::getPlayer();
            MWBase::Environment::get().getWorld()->teleportToClosestMarker(
                player, ESM::RefId::stringRefId("prisonmarker"));
            MWBase::Environment::get().getWindowManager()->fadeScreenOut(
                0.f); // override fade-in caused by cell transition

            setVisible(true);
            mTimeAdvancer.run(100);
        }
    }

    void JailScreen::onJailProgressChanged(std::int32_t cur, std::int32_t /*total*/)
    {
        mProgressBar->setScrollPosition(0);
        mProgressBar->setTrackSize(static_cast<std::int32_t>(static_cast<float>(cur)
            / static_cast<float>(mProgressBar->getScrollRange()) * static_cast<float>(mProgressBar->getLineSize())));
    }

    void JailScreen::onJailFinished()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Jail);
        MWBase::Environment::get().getWindowManager()->fadeScreenIn(0.5);

        MWWorld::Ptr player = MWMechanics::getPlayer();

        MWBase::Environment::get().getMechanicsManager()->rest(mDays * 24, true);
        MWBase::Environment::get().getWorld()->advanceTime(mDays * 24);

        // We should not worsen corprus when in prison
        player.getClass().getCreatureStats(player).getActiveSpells().skipWorsenings(mDays * 24);
        MWBase::Environment::get().getLuaManager()->jailTimeServed(player, mDays);
    }
}
