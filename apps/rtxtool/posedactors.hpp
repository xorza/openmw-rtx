#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <osg/Matrixf>

#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>

#include "motion.hpp"

namespace RtxTool
{
    struct ActorModel;
    struct CellPerson;
    struct CellProp;
    struct Placement;
    class Actor;
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

        /// Whether the region's own residents stand in it as well.
        bool mResidents = true;

        /// Whether the cell's live props — the candles, torches, braziers and fireplaces whose
        /// emitters have to be *run* to hold anything — are instanced and stepped.
        ///
        /// Off leaves them as the shared templates every other reference uses, which is cheaper and
        /// which draws each emitter's authored seed particles rather than a flame.
        bool mProps = true;

        /// Whether people wear what their record carries. Off leaves everyone in their skin.
        bool mClothes = true;

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
        /// Nobody yet, and the world as it currently stands taken as the snapshot.
        ///
        /// @param request read here for what applies to everyone — how they are dressed and where in
        ///        their animation they start — because the residents go in before any row does.
        ///
        /// **The snapshot before anyone is added, and `settle` after everyone is.** Adding builds a
        /// body without placing it; a snapshot taken with one already in it would put a second copy
        /// of that person in the scene on the very next frame.
        PosedActors(
            World& world, Rtx::SceneDesc& scene, RtxBridge::SceneExtractor& extractor, const ActorRequest& request);
        ~PosedActors();

        PosedActors(const PosedActors&) = delete;
        PosedActors& operator=(const PosedActors&) = delete;

        /// Everyone `request` names, standing in a row in front of `placement` and facing it.
        void addRow(const ActorRequest& request, const Placement& placement);

        /// Everyone a cell placed, each where the cell put them.
        void addResidents(std::span<const CellPerson> people);

        /// The cell's live props, each as an instance of its own model.
        ///
        /// **An instance per reference, and it costs no geometry.** A template's particle systems
        /// are shared and never updated, so thirty candles would share one frozen flame; cloning
        /// deep-copies the nodes and the emitters and leaves the plain geometry shared, so the mesh
        /// cache still resolves all thirty candles to one mesh.
        void addProps(std::span<const CellProp> props);

        /// Runs the emitters up to a steady state and walks everyone in, reporting what they came to.
        ///
        /// **The warm-up is what makes a still frame worth looking at.** A particle system loads
        /// holding the seed the file authored — a handful of specks a fifth of a unit across — and
        /// only reaches the flame it is meant to be once its emitter has run for a lifetime or two.
        /// A single frame stepped from nothing integrates nothing, so a shot of a lit room would
        /// show fifty-five candles with no flames on them.
        const RtxBridge::ExtractionStats& settle();

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

        /// Everything being posed, props included.
        std::size_t getCount() const { return mActors.size(); }

        /// How many of those are props rather than people or creatures.
        std::size_t getPropCount() const { return mProps; }

    private:
        void add(ActorModel model, const osg::Matrixf& transform);
        RtxBridge::ExtractionStats place(float seconds);

        /// Poses everyone at `seconds`, having advanced the world by `elapsed`, without walking
        /// them into the scene.
        ///
        /// **Two clocks and not one.** A keyframe track is *sampled* at a time and wrapped to its
        /// own length; an emitter *integrates* and can only go forwards. The warm-up holds the first
        /// still and turns the second, and a window turns both — so the caller says which is which
        /// rather than one being derived from the other.
        void posedAt(float seconds, float elapsed);

        World& mWorld;
        Rtx::SceneDesc& mScene;
        RtxBridge::SceneExtractor& mExtractor;

        /// The world as it stood before any actor went into it, so a frame can put it back.
        std::vector<Rtx::MeshInstance> mStanding;
        std::vector<Rtx::Light> mLit;

        std::vector<std::unique_ptr<Actor>> mActors;

        /// How far into their own animation each of them is, on top of the clock.
        ///
        /// **Without it a town breathes in unison**, which reads as a rank of automata rather than
        /// as people: everyone plays the same idle and nothing in the content offsets them.
        std::vector<float> mPhases;

        RtxBridge::ExtractionStats mPlaced;
        float mSeconds = 0.0f;
        bool mClothes = true;

        /// The animation time the last frame ran at, so the next can say how much time has passed.
        float mLastSeconds = 0.0f;

        /// How many of `mActors` are props. They are posed exactly as an actor is — the emitters
        /// hang off update callbacks, which is what the traversal is for — so they are not kept
        /// apart, only counted.
        std::size_t mProps = 0;

        /// How far a repeated frame carries the animation. Sixty a second, because that is what the
        /// frame budget is written against and an actor should move the same amount per frame here
        /// as it does in the game.
        static constexpr float sFrameSeconds = 1.0f / 60.0f;

        /// How long the emitters are run before the first frame is drawn.
        ///
        /// **Two seconds, which is longer than anything the game emits lives.** The median lifetime
        /// across the shipped emitters is under a second, so by here every seed particle the file
        /// authored has died and been replaced by the emitter's own — which is the steady state a
        /// frame is supposed to show.
        static constexpr float sWarmSeconds = 2.0f;
    };
}
