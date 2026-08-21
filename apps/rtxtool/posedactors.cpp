#include "posedactors.hpp"

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadnpc.hpp>

#include "actor.hpp"
#include "npc.hpp"
#include "placement.hpp"
#include "world.hpp"

namespace RtxTool
{
    PosedActors::PosedActors(World& world, Rtx::SceneDesc& scene, RtxBridge::SceneExtractor& extractor,
        const ActorRequest& request, const Placement& placement)
        : mScene(scene)
        , mExtractor(extractor)
        , mStanding(scene.getInstances().begin(), scene.getInstances().end())
        , mLit(scene.getLights().begin(), scene.getLights().end())
        , mSeconds(request.mSeconds)
    {
        const std::size_t wanted = request.size();
        mActors.reserve(wanted);

        /// Where the next one stands. Counted rather than indexed off the request, because a person
        /// nobody has a record for leaves a gap in the row otherwise.
        const auto next = [&] { return placeActor(placement.mOrigin, placement.mTarget, mActors.size(), wanted); };

        for (const std::string& model : request.mCreatures)
            mActors.push_back(
                std::make_unique<Actor>(world, loadCreature(world, VFS::Path::Normalized(model)), next()));

        for (const std::string& id : request.mPeople)
        {
            const ESM::NPC* who = findNpc(world, id);
            if (who == nullptr)
            {
                Log(Debug::Warning) << "No such person as " << id;
                continue;
            }

            mActors.push_back(std::make_unique<Actor>(world, buildNpc(world, *who), next()));
        }

        mPlaced = place(mSeconds);
    }

    // Out of line because `Actor` is only forward declared in the header.
    PosedActors::~PosedActors() = default;

    bool PosedActors::advanceTo(float seconds)
    {
        if (mActors.empty())
            return false;

        unplace();
        place(seconds);
        mExtractor.advance();
        return true;
    }

    void PosedActors::unplace()
    {
        mScene.clearPlacement();
        for (const Rtx::MeshInstance& instance : mStanding)
            mScene.addInstance(instance);
        for (const Rtx::Light& light : mLit)
            mScene.addLight(light);
    }

    bool PosedActors::step(std::uint32_t frame)
    {
        return advanceTo(mSeconds + static_cast<float>(frame) * sFrameSeconds);
    }

    void PosedActors::restanding()
    {
        mStanding.assign(mScene.getInstances().begin(), mScene.getInstances().end());
        mLit.assign(mScene.getLights().begin(), mScene.getLights().end());
    }

    RtxBridge::ExtractionStats PosedActors::place(float seconds)
    {
        RtxBridge::ExtractionStats stats;
        for (const std::unique_ptr<Actor>& actor : mActors)
        {
            actor->pose(seconds);
            stats += mExtractor.extract(actor->getRoot(), actor->getTransform());
        }

        return stats;
    }
}
