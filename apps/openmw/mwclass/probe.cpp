#include "probe.hpp"

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
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadprob.hpp>

#include "classmodel.hpp"
#include "nameorid.hpp"

namespace MWClass
{
    Probe::Probe()
        : MWWorld::RegisteredClass<Probe>(ESM::Probe::sRecordId)
    {
    }

    void Probe::insertObjectRendering(
        const MWWorld::Ptr& ptr, const std::string& model, MWRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
        }
    }

    VFS::Path::NormalizedView Probe::getModel(const MWWorld::ConstPtr& ptr) const
    {
        return getClassModel<ESM::Probe>(ptr);
    }

    std::string_view Probe::getName(const MWWorld::ConstPtr& ptr) const
    {
        return getNameOrId<ESM::Probe>(ptr);
    }
    std::unique_ptr<MWWorld::Action> Probe::activate(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor) const
    {
        return defaultItemActivate(ptr, actor);
    }

    ESM::RefId Probe::getScript(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Probe>* ref = ptr.get<ESM::Probe>();

        return ref->mBase->mScript;
    }

    std::pair<std::vector<std::int32_t>, bool> Probe::getEquipmentSlots(const MWWorld::ConstPtr& ptr) const
    {
        std::vector<std::int32_t> slots;

        slots.push_back(static_cast<std::int32_t>(MWWorld::InventoryStore::Slot_CarriedRight));

        return std::make_pair(slots, false);
    }

    std::int32_t Probe::getValue(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Probe>* ref = ptr.get<ESM::Probe>();

        return ref->mBase->mData.mValue;
    }

    const ESM::RefId& Probe::getUpSoundId(const MWWorld::ConstPtr& ptr) const
    {
        static const ESM::RefId sound = ESM::RefId::stringRefId("Item Probe Up");
        return sound;
    }

    const ESM::RefId& Probe::getDownSoundId(const MWWorld::ConstPtr& ptr) const
    {
        static const ESM::RefId sound = ESM::RefId::stringRefId("Item Probe Down");
        return sound;
    }

    VFS::Path::NormalizedView Probe::getInventoryIcon(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Probe>* ref = ptr.get<ESM::Probe>();

        return ref->mBase->mIcon.getNormalized();
    }

    MWGui::ToolTipInfo Probe::getToolTipInfo(const MWWorld::ConstPtr& ptr, std::int32_t count) const
    {
        const MWWorld::LiveCellRef<ESM::Probe>* ref = ptr.get<ESM::Probe>();

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

    std::unique_ptr<MWWorld::Action> Probe::use(const MWWorld::Ptr& ptr, bool force) const
    {
        std::unique_ptr<MWWorld::Action> action = std::make_unique<MWWorld::ActionEquip>(ptr, force);

        action->setSound(getUpSoundId(ptr));

        return action;
    }

    MWWorld::Ptr Probe::copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const
    {
        const MWWorld::LiveCellRef<ESM::Probe>* ref = ptr.get<ESM::Probe>();

        return MWWorld::Ptr(cell.insert(ref), &cell);
    }

    std::pair<std::int32_t, std::string_view> Probe::canBeEquipped(
        const MWWorld::ConstPtr& ptr, const MWWorld::Ptr& npc) const
    {
        // Do not allow equip tools from inventory during attack
        if (MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(npc)
            && !MWBase::Environment::get().getMechanicsManager()->isCastingSpell(npc)
            && MWBase::Environment::get().getWindowManager()->isGuiMode())
            return { 0, "#{sCantEquipWeapWarning}" };

        return { 1, {} };
    }

    bool Probe::canSell(const MWWorld::ConstPtr& item, std::int32_t npcServices) const
    {
        return (npcServices & ESM::NPC::Probes) != 0;
    }

    std::int32_t Probe::getItemMaxHealth(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Probe>* ref = ptr.get<ESM::Probe>();

        return ref->mBase->mData.mUses;
    }

    float Probe::getWeight(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Probe>* ref = ptr.get<ESM::Probe>();
        return ref->mBase->mData.mWeight;
    }
}
