#include "ingredient.hpp"

#include <cstdint>

#include <MyGUI_TextIterator.h>
#include <MyGUI_UString.h>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwgui/tooltips.hpp>
#include <apps/openmw/mwrender/objects.hpp>
#include <apps/openmw/mwrender/renderinginterface.hpp>
#include <apps/openmw/mwworld/actioneat.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/nullaction.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadnpc.hpp>

#include "classmodel.hpp"
#include "nameorid.hpp"

namespace MWClass
{
    Ingredient::Ingredient()
        : MWWorld::RegisteredClass<Ingredient>(ESM::Ingredient::sRecordId)
    {
    }

    void Ingredient::insertObjectRendering(
        const MWWorld::Ptr& ptr, const std::string& model, MWRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
        }
    }

    VFS::Path::NormalizedView Ingredient::getModel(const MWWorld::ConstPtr& ptr) const
    {
        return getClassModel<ESM::Ingredient>(ptr);
    }

    std::string_view Ingredient::getName(const MWWorld::ConstPtr& ptr) const
    {
        return getNameOrId<ESM::Ingredient>(ptr);
    }

    std::unique_ptr<MWWorld::Action> Ingredient::activate(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor) const
    {
        return defaultItemActivate(ptr, actor);
    }

    ESM::RefId Ingredient::getScript(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Ingredient>* ref = ptr.get<ESM::Ingredient>();

        return ref->mBase->mScript;
    }

    std::int32_t Ingredient::getValue(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Ingredient>* ref = ptr.get<ESM::Ingredient>();

        return ref->mBase->mData.mValue;
    }

    std::unique_ptr<MWWorld::Action> Ingredient::use(const MWWorld::Ptr& ptr, bool force) const
    {
        if (ptr.get<ESM::Ingredient>()->mBase->mData.mEffectID[0].empty())
            return std::make_unique<MWWorld::NullAction>();
        std::unique_ptr<MWWorld::Action> action = std::make_unique<MWWorld::ActionEat>(ptr);

        action->setSound(ESM::RefId::stringRefId("Swallow"));

        return action;
    }

    const ESM::RefId& Ingredient::getUpSoundId(const MWWorld::ConstPtr& ptr) const
    {
        static auto sound = ESM::RefId::stringRefId("Item Ingredient Up");
        return sound;
    }

    const ESM::RefId& Ingredient::getDownSoundId(const MWWorld::ConstPtr& ptr) const
    {
        static auto sound = ESM::RefId::stringRefId("Item Ingredient Down");
        return sound;
    }

    VFS::Path::NormalizedView Ingredient::getInventoryIcon(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Ingredient>* ref = ptr.get<ESM::Ingredient>();

        return ref->mBase->mIcon.getNormalized();
    }

    MWGui::ToolTipInfo Ingredient::getToolTipInfo(const MWWorld::ConstPtr& ptr, std::int32_t count) const
    {
        const MWWorld::LiveCellRef<ESM::Ingredient>* ref = ptr.get<ESM::Ingredient>();

        MWGui::ToolTipInfo info;
        std::string_view name = getName(ptr);
        info.caption = MyGUI::TextIterator::toTagsString(MyGUI::UString(name)) + MWGui::ToolTips::getCountString(count);
        info.icon = ref->mBase->mIcon.getOriginal();

        std::string text;

        text += MWGui::ToolTips::getWeightString(ref->mBase->mData.mWeight, "#{sWeight}");
        text += MWGui::ToolTips::getValueString(ref->mBase->mData.mValue, "#{sValue}");

        if (MWBase::Environment::get().getWindowManager()->getFullHelp())
        {
            info.extra += MWGui::ToolTips::getCellRefString(ptr.getCellRef());
            info.extra += MWGui::ToolTips::getMiscString(ref->mBase->mScript.getRefIdString(), "Script");
        }

        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        float alchemySkill = player.getClass().getSkill(player, ESM::Skill::Alchemy);

        static const float fWortChanceValue = MWBase::Environment::get()
                                                  .getESMStore()
                                                  ->get<ESM::GameSetting>()
                                                  .find("fWortChanceValue")
                                                  ->mValue.getFloat();

        MWGui::Widgets::SpellEffectList list;
        for (std::int32_t i = 0; i < 4; ++i)
        {
            if (ref->mBase->mData.mEffectID[i].empty())
                continue;
            MWGui::Widgets::SpellEffectParams params;
            params.mEffectID = ref->mBase->mData.mEffectID[i];
            params.mAttribute = ref->mBase->mData.mAttributes[i];
            params.mSkill = ref->mBase->mData.mSkills[i];
            params.mKnown = alchemySkill >= fWortChanceValue * static_cast<float>((i + 1));

            list.push_back(params);
        }
        info.effects = std::move(list);

        info.text = std::move(text);
        info.isIngredient = true;

        return info;
    }

    MWWorld::Ptr Ingredient::copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const
    {
        const MWWorld::LiveCellRef<ESM::Ingredient>* ref = ptr.get<ESM::Ingredient>();

        return MWWorld::Ptr(cell.insert(ref), &cell);
    }

    bool Ingredient::canSell(const MWWorld::ConstPtr& item, std::int32_t npcServices) const
    {
        return (npcServices & ESM::NPC::Ingredients) != 0;
    }

    float Ingredient::getWeight(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Ingredient>* ref = ptr.get<ESM::Ingredient>();
        return ref->mBase->mData.mWeight;
    }
}
