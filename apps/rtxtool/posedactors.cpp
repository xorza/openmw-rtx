#include "posedactors.hpp"

#include <cmath>

#include <components/debug/debuglog.hpp>
#include <components/esm3/loadnpc.hpp>

#include "actor.hpp"
#include "cellscene.hpp"
#include "npc.hpp"
#include "placement.hpp"
#include "world.hpp"

namespace RtxTool
{
    namespace
    {
        /// How far into their own idle somebody standing at `where` is.
        ///
        /// **Derived from where they stand rather than drawn at random**, so a shot of a town is the
        /// same shot twice. Any offset would do — all that matters is that no two people share one,
        /// and a position is the one thing about a resident that already differs.
        float phaseAt(const osg::Matrixf& where)
        {
            const osg::Vec3f at = where.getTrans();
            const float mixed = at.x() * 0.31718f + at.y() * 0.17321f + at.z() * 0.07734f;
            return std::abs(std::fmod(mixed, 1.0f));
        }
    }

    PosedActors::PosedActors(World& world, Rtx::SceneDesc& scene, RtxBridge::SceneExtractor& extractor)
        : mWorld(world)
        , mScene(scene)
        , mExtractor(extractor)
        , mStanding(scene.getInstances().begin(), scene.getInstances().end())
        , mLit(scene.getLights().begin(), scene.getLights().end())
    {
    }

    // Out of line because `Actor` is only forward declared in the header.
    PosedActors::~PosedActors() = default;

    void PosedActors::add(ActorModel model, const osg::Matrixf& transform)
    {
        mActors.push_back(std::make_unique<Actor>(mWorld, std::move(model), transform));

        // Nudged per actor as well as per position, so a row in front of a camera — spread along one
        // axis, which walks the offset in step — comes out scattered rather than in a wave.
        mPhases.push_back(phaseAt(transform) + static_cast<float>(mActors.size()) * 0.137f);
    }

    void PosedActors::addRow(const ActorRequest& request, const Placement& placement)
    {
        mSeconds = request.mSeconds;

        const std::size_t wanted = request.size();

        // Counted off what has actually been added, so somebody nobody has a record for leaves no
        // gap in the row.
        const auto next = [&] { return placeActor(placement.mOrigin, placement.mTarget, mActors.size(), wanted); };

        for (const std::string& model : request.mCreatures)
            add(loadCreature(mWorld, VFS::Path::Normalized(model)), next());

        for (const std::string& id : request.mPeople)
        {
            const ESM::NPC* who = findNpc(mWorld, id);
            if (who == nullptr)
            {
                Log(Debug::Warning) << "No such person as " << id;
                continue;
            }

            add(buildNpc(mWorld, *who), next());
        }
    }

    void PosedActors::addResidents(std::span<const CellPerson> people)
    {
        for (const CellPerson& person : people)
            if (person.mRecord != nullptr)
                add(buildNpc(mWorld, *person.mRecord), person.mTransform);
    }

    const RtxBridge::ExtractionStats& PosedActors::settle()
    {
        mPlaced = place(mSeconds);
        return mPlaced;
    }

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
        for (std::size_t at = 0; at < mActors.size(); ++at)
        {
            const std::unique_ptr<Actor>& actor = mActors[at];
            actor->pose(seconds + mPhases[at] * actor->getDuration());
            stats += mExtractor.extract(actor->getRoot(), actor->getTransform());
        }

        return stats;
    }
}
