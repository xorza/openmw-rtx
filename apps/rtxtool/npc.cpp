#include "npc.hpp"

#include <algorithm>
#include <array>

#include <osg/Group>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/attach.hpp>
#include <components/sceneutil/skeleton.hpp>
#include <components/sceneutil/visitor.hpp>
#include <components/settings/values.hpp>

#include "actor.hpp"
#include "world.hpp"

namespace RtxTool
{
    namespace
    {
        /// One limb of a naked body: which kind of `BODY` record fills it, and where it hangs.
        ///
        /// The filter is the bone name as well, because a body part model holds every copy of itself
        /// at once — one hand mesh contains both hands — and the filter is what picks the skinned
        /// subtree belonging to this bone out of it. That is also why the paired limbs appear twice
        /// with one part between them.
        struct BodySlot
        {
            ESM::BodyPart::MeshPart mPart;
            std::string_view mBone;
        };

        /// What a naked person in the third person is made of.
        ///
        /// The head and the hair are not here: those two are named by the NPC record itself rather
        /// than chosen by race, which is the whole of how one Dunmer is told from another.
        constexpr std::array sBodySlots{
            BodySlot{ ESM::BodyPart::MP_Neck, "Neck" },
            BodySlot{ ESM::BodyPart::MP_Chest, "Chest" },
            BodySlot{ ESM::BodyPart::MP_Groin, "Groin" },
            BodySlot{ ESM::BodyPart::MP_Hand, "Right Hand" },
            BodySlot{ ESM::BodyPart::MP_Hand, "Left Hand" },
            BodySlot{ ESM::BodyPart::MP_Wrist, "Right Wrist" },
            BodySlot{ ESM::BodyPart::MP_Wrist, "Left Wrist" },
            BodySlot{ ESM::BodyPart::MP_Forearm, "Right Forearm" },
            BodySlot{ ESM::BodyPart::MP_Forearm, "Left Forearm" },
            BodySlot{ ESM::BodyPart::MP_Upperarm, "Right Upper Arm" },
            BodySlot{ ESM::BodyPart::MP_Upperarm, "Left Upper Arm" },
            BodySlot{ ESM::BodyPart::MP_Foot, "Right Foot" },
            BodySlot{ ESM::BodyPart::MP_Foot, "Left Foot" },
            BodySlot{ ESM::BodyPart::MP_Ankle, "Right Ankle" },
            BodySlot{ ESM::BodyPart::MP_Ankle, "Left Ankle" },
            BodySlot{ ESM::BodyPart::MP_Knee, "Right Knee" },
            BodySlot{ ESM::BodyPart::MP_Knee, "Left Knee" },
            BodySlot{ ESM::BodyPart::MP_Upperleg, "Right Upper Leg" },
            BodySlot{ ESM::BodyPart::MP_Upperleg, "Left Upper Leg" },
            BodySlot{ ESM::BodyPart::MP_Tail, "Tail" },
        };

        bool isFemalePart(const ESM::BodyPart& part)
        {
            return (part.mData.mFlags & ESM::BodyPart::BPF_Female) != 0;
        }

        /// Whether a body part is skin this race can wear in the third person.
        bool wearable(const ESM::BodyPart& part)
        {
            return (part.mData.mFlags & ESM::BodyPart::BPF_NotPlayable) == 0
                && part.mData.mType == ESM::BodyPart::MT_Skin && !ESM::isFirstPersonBodyPart(part);
        }

        /// The skin `race` has for `want`, preferring `female` and falling back to the other sex.
        ///
        /// **The fallback is the game's and not a convenience.** Morrowind ships races whose female
        /// body is only partly authored, and a person missing a forearm because nobody drew a female
        /// one is a hole in the middle of them.
        const ESM::BodyPart* findSkin(
            const World& world, const ESM::RefId& race, ESM::BodyPart::MeshPart want, bool female)
        {
            const ESM::BodyPart* fallback = nullptr;

            for (const ESM::BodyPart& part : world.getRecords<ESM::BodyPart>())
            {
                if (!wearable(part) || part.mRace != race || part.mData.mPart != want)
                    continue;

                if (isFemalePart(part) == female)
                    return &part;

                if (fallback == nullptr)
                    fallback = &part;
            }

            return fallback;
        }

        /// Whether `race` walks on the beast skeleton.
        ///
        /// **Asked of the body parts rather than of the race record**, which the harness does not
        /// load: the two beast races are exactly the two with a tail to skin, and deriving it from
        /// the content beats a list of two names that a content file could extend.
        bool isBeast(const World& world, const ESM::RefId& race)
        {
            for (const ESM::BodyPart& part : world.getRecords<ESM::BodyPart>())
                if (wearable(part) && part.mRace == race && part.mData.mPart == ESM::BodyPart::MP_Tail)
                    return true;

            return false;
        }

        /// Which shared skeleton this person's parts hang on.
        ///
        /// Read off the same settings the game reads, so a content file that replaces the base
        /// animation replaces it here too. One beast skeleton serves both sexes, which is Morrowind's
        /// own arrangement and not a simplification.
        VFS::Path::Normalized skeletonFor(const World& world, bool female, bool beast)
        {
            const VFS::Path::Normalized& named = beast ? Settings::models().mBaseanimkna.get()
                : female                               ? Settings::models().mBaseanimfemale.get()
                                                       : Settings::models().mBaseanim.get();

            return Misc::ResourceHelpers::correctActorModelPath(named, world.getResourceSystem().getVFS());
        }

        /// Hangs one body part model on `bone`, taking the piece of it the bone is for.
        void hang(World& world, osg::Group& skeleton, const SceneUtil::NodeMap& bones, const ESM::BodyPart& part,
            std::string_view bone, std::string_view filter)
        {
            const auto found = bones.find(std::string(bone));
            if (found == bones.end())
                return;

            Resource::SceneManager& scene = world.getSceneManager();
            const VFS::Path::Normalized model = Misc::ResourceHelpers::correctMeshPath(part.mModel.getNormalized());

            try
            {
                SceneUtil::attach(scene.getTemplate(model), &skeleton, filter, found->second.get(), &scene);
            }
            catch (const std::exception& failed)
            {
                Log(Debug::Warning) << "Cannot hang " << model << " on " << bone << ": " << failed.what();
            }
        }
    }

    const ESM::NPC* findNpc(const World& world, std::string_view id)
    {
        const ESM::RefId wanted = ESM::RefId::stringRefId(id);
        for (const ESM::NPC& npc : world.getRecords<ESM::NPC>())
            if (npc.mId == wanted)
                return &npc;

        return nullptr;
    }

    ActorModel buildNpc(World& world, const ESM::NPC& npc)
    {
        const bool female = !npc.isMale();
        const VFS::Path::Normalized skeleton = skeletonFor(world, female, isBeast(world, npc.mRace));

        ActorModel built;
        built.mSkeleton = skeleton;

        // An instance rather than a template, because parts are about to be hung on this one's bones
        // and the template is shared with everyone else of this sex.
        osg::ref_ptr<osg::Node> created = world.getSceneManager().getInstance(skeleton);
        built.mRoot = dynamic_cast<SceneUtil::Skeleton*>(created.get());
        if (built.mRoot == nullptr)
        {
            osg::ref_ptr<SceneUtil::Skeleton> wrapper = new SceneUtil::Skeleton;
            wrapper->addChild(created);
            built.mRoot = wrapper;
        }

        // **The skeleton is bones and nothing else.** Morrowind's base animation files ship with
        // placeholder geometry in them — the female one most visibly — and a person built without
        // stripping it wears a magenta torso and a grey chevron nobody drew. The game does the same
        // thing under the name `baseonly`, and this is why an NPC's skeleton cannot simply be loaded
        // the way a creature's is.
        SceneUtil::CleanObjectRootVisitor bare;
        built.mRoot->accept(bare);
        bare.remove();

        SceneUtil::NodeMap bones;
        SceneUtil::NodeMapVisitor collect(bones);
        built.mRoot->accept(collect);

        for (const BodySlot& slot : sBodySlots)
            if (const ESM::BodyPart* part = findSkin(world, npc.mRace, slot.mPart, female))
                hang(world, *built.mRoot, bones, *part, slot.mBone, slot.mBone);

        // **Both on the head bone, and the hair is filtered by name rather than by bone.** Morrowind
        // models a hairstyle as a second skinned mesh over the skull, so the two would take each
        // other's triangles if the filter went by where they hang.
        if (const ESM::BodyPart* head = world.findRecord<ESM::BodyPart>(npc.mHead))
            hang(world, *built.mRoot, bones, *head, "Head", "Head");

        if (const ESM::BodyPart* hair = world.findRecord<ESM::BodyPart>(npc.mHair))
            hang(world, *built.mRoot, bones, *hair, "Head", "Hair");

        return built;
    }
}
