#include "activator.hpp"

#include <MyGUI_TextIterator.h>
#include <MyGUI_UString.h>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwgui/tooltips.hpp>
#include <apps/openmw/mwphysics/physicssystem.hpp>
#include <apps/openmw/mwrender/objects.hpp>
#include <apps/openmw/mwrender/renderinginterface.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <apps/openmw/mwworld/action.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/nullaction.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadsndg.hpp>
#include <components/misc/rng.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "classmodel.hpp"

namespace MWClass
{
    Activator::Activator()
        : MWWorld::RegisteredClass<Activator>(ESM::Activator::sRecordId)
    {
    }

    void Activator::insertObjectRendering(
        const MWWorld::Ptr& ptr, const std::string& model, MWRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
            ptr.getRefData().getBaseNode()->setNodeMask(MWRender::Mask_Static);
        }
    }

    void Activator::insertObject(const MWWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        insertObjectPhysics(ptr, model, rotation, physics);
    }

    void Activator::insertObjectPhysics(const MWWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
        MWPhysics::PhysicsSystem& physics) const
    {
        physics.addObject(ptr, VFS::Path::toNormalized(model), rotation, MWPhysics::CollisionType_World);
    }

    VFS::Path::NormalizedView Activator::getModel(const MWWorld::ConstPtr& ptr) const
    {
        return getClassModel<ESM::Activator>(ptr);
    }

    bool Activator::isActivator() const
    {
        return true;
    }

    bool Activator::useAnim() const
    {
        return true;
    }

    std::string_view Activator::getName(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        return ref->mBase->mName;
    }

    ESM::RefId Activator::getScript(const MWWorld::ConstPtr& ptr) const
    {
        const MWWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        return ref->mBase->mScript;
    }

    bool Activator::hasToolTip(const MWWorld::ConstPtr& ptr) const
    {
        return !getName(ptr).empty();
    }

    MWGui::ToolTipInfo Activator::getToolTipInfo(const MWWorld::ConstPtr& ptr, int count) const
    {
        const MWWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        MWGui::ToolTipInfo info;
        std::string_view name = getName(ptr);
        info.caption = MyGUI::TextIterator::toTagsString(MyGUI::UString(name)) + MWGui::ToolTips::getCountString(count);

        if (MWBase::Environment::get().getWindowManager()->getFullHelp())
        {
            info.extra += MWGui::ToolTips::getCellRefString(ptr.getCellRef());
            info.extra += MWGui::ToolTips::getMiscString(ref->mBase->mScript.getRefIdString(), "Script");
        }

        return info;
    }

    std::unique_ptr<MWWorld::Action> Activator::activate(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor) const
    {
        std::unique_ptr<MWWorld::Action> werewolfAction = getWerewolfRefusalAction(actor);
        if (werewolfAction)
            return werewolfAction;

        return std::make_unique<MWWorld::NullAction>();
    }

    MWWorld::Ptr Activator::copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const
    {
        const MWWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        return MWWorld::Ptr(cell.insert(ref), &cell);
    }

    ESM::RefId Activator::getSoundIdFromSndGen(const MWWorld::Ptr& ptr, std::string_view name) const
    {
        // Assume it's not empty, since we wouldn't have gotten the soundgen otherwise
        const VFS::Path::NormalizedView model = getModel(ptr);
        const MWWorld::ESMStore& store = *MWBase::Environment::get().getESMStore();
        ESM::RefId creatureId;

        for (const ESM::Creature& iter : store.get<ESM::Creature>())
        {
            if (!iter.mModel.empty() && model == iter.mModel.getNormalized())
            {
                creatureId = !iter.mOriginal.empty() ? iter.mOriginal : iter.mId;
                break;
            }
        }

        const int type = getSndGenTypeFromName(name);

        std::vector<const ESM::SoundGenerator*> fallbacksounds;
        auto& prng = MWBase::Environment::get().getWorld()->getPrng();
        if (!creatureId.empty())
        {
            std::vector<const ESM::SoundGenerator*> sounds;
            for (const auto& sound : store.get<ESM::SoundGenerator>())
            {
                if (type == sound.mType && !sound.mCreature.empty() && creatureId == sound.mCreature)
                    sounds.push_back(&sound);
                if (type == sound.mType && sound.mCreature.empty())
                    fallbacksounds.push_back(&sound);
            }

            if (!sounds.empty())
                return sounds[Misc::Rng::rollDice(sounds.size(), prng)]->mSound;
            if (!fallbacksounds.empty())
                return fallbacksounds[Misc::Rng::rollDice(fallbacksounds.size(), prng)]->mSound;
        }
        else
        {
            // The activator doesn't have a corresponding creature ID, but we can try to use the defaults
            for (const auto& sound : store.get<ESM::SoundGenerator>())
                if (type == sound.mType && sound.mCreature.empty())
                    fallbacksounds.push_back(&sound);

            if (!fallbacksounds.empty())
                return fallbacksounds[Misc::Rng::rollDice(fallbacksounds.size(), prng)]->mSound;
        }

        return ESM::RefId();
    }

    int Activator::getSndGenTypeFromName(std::string_view name)
    {
        if (name == "left")
            return ESM::SoundGenerator::LeftFoot;
        if (name == "right")
            return ESM::SoundGenerator::RightFoot;
        if (name == "swimleft")
            return ESM::SoundGenerator::SwimLeft;
        if (name == "swimright")
            return ESM::SoundGenerator::SwimRight;
        if (name == "moan")
            return ESM::SoundGenerator::Moan;
        if (name == "roar")
            return ESM::SoundGenerator::Roar;
        if (name == "scream")
            return ESM::SoundGenerator::Scream;
        if (name == "land")
            return ESM::SoundGenerator::Land;

        throw std::runtime_error("Unexpected soundgen type: " + std::string(name));
    }
}
