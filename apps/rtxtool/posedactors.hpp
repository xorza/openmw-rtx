#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>

#include "motion.hpp"

namespace RtxTool
{
    class Actor;
    class Placement;
    class World;

    /// Who to put in front of the camera.
    struct ActorRequest
    {
        /// Creature model paths, as a `CREA` record holds them.
        std::vector<std::string> mCreatures;

        /// `NPC_` record ids. Each is assembled out of their race's body parts.
        std::vector<std::string> mPeople;

        /// How many seconds into their animation they start.
        float mSeconds = 0.0f;

        bool empty() const { return mCreatures.empty() && mPeople.empty(); }
        std::size_t size() const { return mCreatures.size() + mPeople.size(); }
    };

    /// A row of actors, posed and walked into the scene once per frame.
    ///
    /// **The still world is restored from a snapshot rather than mirrored again.** The game re-walks
    /// its whole graph every frame and can, because it has one; the harness does not — a cell is a
    /// template fetched per reference and a terrain grid built cell by cell, and walking that again
    /// per frame is the load again per frame. What a frame actually needs is that the placements be
    /// there and the deforming meshes be re-read, and a list copied back gives the first exactly as
    /// a re-walk would.
    class PosedActors : public Motion
    {
    public:
        /// Builds everyone `request` names and walks them in where `placement` is looking.
        ///
        /// The scene must already hold the world they are standing in: the snapshot is taken here.
        PosedActors(World& world, Rtx::SceneDesc& scene, RtxBridge::SceneExtractor& extractor,
            const ActorRequest& request, const Placement& placement);
        ~PosedActors();

        PosedActors(const PosedActors&) = delete;
        PosedActors& operator=(const PosedActors&) = delete;

        /// Advances to `seconds` and walks everyone back in. False where there is nobody.
        bool advanceTo(float seconds);

        bool step(std::uint32_t frame) override;

        /// Takes the actors back out, leaving the world as it stood.
        ///
        /// **What a window calls before it loads the next ring of cells.** Those are walked into
        /// whatever the scene currently holds, and a scene still holding this frame's actors would
        /// take them into the snapshot and place a second copy of each on the frame after.
        void unplace();

        /// Takes the world as it now stands to be what a frame puts back. The other half of
        /// `unplace`, called once the cells that arrived between them are in.
        void restanding();

        /// What the actors added the first time they were walked in.
        const RtxBridge::ExtractionStats& getPlaced() const { return mPlaced; }

        std::size_t getCount() const { return mActors.size(); }

    private:
        RtxBridge::ExtractionStats place(float seconds);

        Rtx::SceneDesc& mScene;
        RtxBridge::SceneExtractor& mExtractor;

        /// The world as it stood before any actor went into it, so a frame can put it back.
        std::vector<Rtx::MeshInstance> mStanding;
        std::vector<Rtx::Light> mLit;

        std::vector<std::unique_ptr<Actor>> mActors;
        RtxBridge::ExtractionStats mPlaced;
        float mSeconds = 0.0f;

        /// How far a repeated frame carries the animation. Sixty a second, because that is what the
        /// frame budget is written against and an actor should move the same amount per frame here
        /// as it does in the game.
        static constexpr float sFrameSeconds = 1.0f / 60.0f;
    };
}
