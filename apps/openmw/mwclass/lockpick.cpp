#include "lockpick.hpp"

#include <cstdint>

#include <MyGUI_TextIterator.h>
#include <MyGUI_UString.h>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwgui/tooltips.hpp>
#include <apps/openmw/mwrender/objects.hpp>
#include <apps/openmw/mwrender/renderinginterface.hpp>
#include <apps/openmw/mwworld/actionequip.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/inventorystore.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadnpc.hpp>

#include "classmodel.hpp"
#include "nameorid.hpp"

namespace MWClass
{
    Lockpick::Lockpick()
        : MWWorld::RegisteredClass<Lockpick>(ESM::Lockpick::sRecordId)
    {
    }

    void Lockpick::insertObjectRendering(
        const MWWorld::Ptr& ptr, const std::string& model, MWRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
        }
    }

    VFS::Path::NormalizedView Lockpick::getModel(const MWWorld::ConstPtr& ptr) const
    {
        return getClassModel<ESM::Lockpick>(ptr);
    }

    std::string_view Lockpick::getName(const MWWorld::ConstPtr& ptr) const
    {
        return getNameOrId<ESM::Lockpick>(ptr);
    }

    std::unique_ptr<MWWorld::Action> Lockpick::activate(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor) const
    {
        return defaultItemActivate(ptr, actor);
    }

    ESM::RefId Lockpick::getScript(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Lockpick>* ref = ptr.get<ESM::Lockpick>();

        return ref->mBase->mScript;
    }

    std::pair<std::vector<std::int32_t>, bool> Lockpick::getEquipmentSlots(const MWWorld::ConstPtr& ptr) const
    {
        std::vector<std::int32_t> slots;

        slots.push_back(static_cast<std::int32_t>(MWWorld::InventoryStore::Slot_CarriedRight));

        return std::make_pair(slots, false);
    }

    std::int32_t Lockpick::getValue(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Lockpick>* ref = ptr.get<ESM::Lockpick>();

        return ref->mBase->mData.mValue;
    }

    const ESM::RefId& Lockpick::getUpSoundId(const MWWorld::ConstPtr& ptr) const
    {
        static ESM::RefId sound = ESM::RefId::stringRefId("Item Lockpick Up");
        return sound;
    }

    const ESM::RefId& Lockpick::getDownSoundId(const MWWorld::ConstPtr& ptr) const
    {
        static ESM::RefId sound = ESM::RefId::stringRefId("Item Lockpick Down");
        return sound;
    }

    VFS::Path::NormalizedView Lockpick::getInventoryIcon(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Lockpick>* ref = ptr.get<ESM::Lockpick>();

        return ref->mBase->mIcon.getNormalized();
    }

    MWGui::ToolTipInfo Lockpick::getToolTipInfo(const MWWorld::ConstPtr& ptr, std::int32_t count) const
    {
        const MWWorld::LiveCellRef<ESM::Lockpick>* ref = ptr.get<ESM::Lockpick>();

        MWGui::ToolTipInfo info;
        std::string_view name = getName(ptr);
        info.caption = MyGUI::TextIterator::toTagsString(MyGUI::UString(name)) + MWGui::ToolTips::getCountString(count);
        info.icon = ref->mBase->mIcon.getOriginal();

        std::string text;

        std::int32_t remainingUses = getItemHealth(ptr);

        text += "\n#{sUses}: " + MWGui::ToolTips::toString(remainingUses);
        text += "\n#{sQuality}: " + MWGui::ToolTips::toString(ref->mBase->mData.mQuality);
        text += MWGui::ToolTips::getWeightString(ref->mBase->mData.mWeight, "#{sWeight}");
        text += MWGui::ToolTips::getValueString(ref->mBase->mData.mValue, "#{sValue}");

        if (MWBase::Environment::get().getWindowManager()->getFullHelp())
        {
            info.extra += MWGui::ToolTips::getCellRefString(ptr.getCellRef());
            info.extra += MWGui::ToolTips::getMiscString(ref->mBase->mScript.getRefIdString(), "Script");
        }

        info.text = std::move(text);

        return info;
    }

    std::unique_ptr<MWWorld::Action> Lockpick::use(const MWWorld::Ptr& ptr, bool force) const
    {
        std::unique_ptr<MWWorld::Action> action = std::make_unique<MWWorld::ActionEquip>(ptr, force);

        action->setSound(getUpSoundId(ptr));

        return action;
    }

    MWWorld::Ptr Lockpick::copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const
    {
        const MWWorld::LiveCellRef<ESM::Lockpick>* ref = ptr.get<ESM::Lockpick>();

        return MWWorld::Ptr(cell.insert(ref), &cell);
    }

    std::pair<std::int32_t, std::string_view> Lockpick::canBeEquipped(
        const MWWorld::ConstPtr& ptr, const MWWorld::Ptr& npc) const
    {
        // Do not allow equip tools from inventory during attack
        if (MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(npc)
            && !MWBase::Environment::get().getMechanicsManager()->isCastingSpell(npc)
            && MWBase::Environment::get().getWindowManager()->isGuiMode())
            return { 0, "#{sCantEquipWeapWarning}" };

        return { 1, {} };
    }

    bool Lockpick::canSell(const MWWorld::ConstPtr& item, std::int32_t npcServices) const
    {
        return (npcServices & ESM::NPC::Picks) != 0;
    }

    std::int32_t Lockpick::getItemMaxHealth(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Lockpick>* ref = ptr.get<ESM::Lockpick>();

        return ref->mBase->mData.mUses;
    }

    float Lockpick::getWeight(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Lockpick>* ref = ptr.get<ESM::Lockpick>();
        return ref->mBase->mData.mWeight;
    }
}
