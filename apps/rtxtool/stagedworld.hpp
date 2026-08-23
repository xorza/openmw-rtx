#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include <osg/Vec3f>

#include <osg/Group>

#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>

#include "cellscene.hpp"
#include "lighting.hpp"
#include "placement.hpp"
#include "posedactors.hpp"

namespace ESM
{
    struct Cell;
}

namespace RtxTool
{
    class World;

    /// What a step of the camera cost the graph, or all zeroes where it stayed in its square.
    struct Crossing
    {
        /// How many cells the ring brought in. Fewer than three at a coastline, where the sea has
        /// no cell record to read.
        std::uint32_t mArrived = 0;

        /// How many were taken off the graph behind it.
        std::uint32_t mDeparted = 0;

        /// Whether anything happened at all, which is what says the scene has to be handed over
        /// rather than placed.
        bool happened() const { return mArrived > 0 || mDeparted > 0; }
    };

    /// What has to be decided before a region can be read, beyond which cell it is.
    struct StagingRequest
    {
        /// When and in what weather, for the exterior that has a sky.
        std::string mWeather = "Clear";
        float mHour = 12.0f;

        float mFieldOfView = 60.0f;

        /// Where to stand and what to look at. Either left out is derived from the cell's own
        /// bounds, which is a fine establishing shot for a town and a poor one for a room.
        std::optional<osg::Vec3f> mOrigin;
        std::optional<osg::Vec3f> mTarget;
    };

    /// A region of the world read into a scene, with its people and props posed into it.
    ///
    /// **Everything between "which cell" and "trace it", and it is the same everywhere.** A shot
    /// and a profiling run differ in what they do with the frames and in nothing before them, and
    /// the order here is not arbitrary: the camera is pinned from the world's own bounds *before*
    /// anyone stands in it, because a row of actors is placed relative to where the camera ends up
    /// and bounds that already contain them would put it somewhere else.
    ///
    /// **It streams, so a camera that goes somewhere is not a second implementation of all this.**
    /// `moveTo` brings the ring the camera has walked into and takes the cells behind it off the
    /// graph; the window and a route-following bench both cross cells through it, which is what
    /// stops the two drifting apart again.
    class StagedWorld
    {
    public:
        StagedWorld(World& world, const ESM::Cell& cell, const StagingRequest& request, const ActorRequest& actors);
        ~StagedWorld();

        StagedWorld(const StagedWorld&) = delete;
        StagedWorld& operator=(const StagedWorld&) = delete;

        /// True where the region placed no geometry at all, which nothing can be traced from.
        bool empty() const { return mScene.getPlacedCount() == 0; }

        const Rtx::SceneDesc& getScene() const { return mScene; }

        /// Mutable for whoever hands it to a backend, which consumes the arrivals it holds.
        Rtx::SceneDesc& getScene() { return mScene; }
        const CellLighting& getLighting() const { return mLighting; }
        const Placement& getPlacement() const { return mPlacement; }

        /// What advances the scene between frames by frame index, or null where nothing in it
        /// moves. Borrowed: it does not outlive this.
        ///
        /// **By index and not by the clock**, which is what makes a run of frames reproducible: a
        /// world stepped by how long the last frame took renders a different sequence on every
        /// machine. A window wants the other one — see `advanceTo`.
        Motion* getMotion() { return mPosed.get(); }

        /// Advances the world to `seconds` and walks whatever moved back in. False where nothing
        /// did, which is what spares the frame a hand-over it does not need.
        ///
        /// **The window's clock.** Frames it dropped would otherwise animate in slow motion; a run
        /// being measured wants `getMotion` instead, for the reason above.
        bool advanceTo(float seconds);

        /// Brings the region around `where` in and takes the cells that left off the graph.
        ///
        /// **The camera's own cell is what triggers it, not a distance**, which is the game's rule
        /// and so this one: a step that stays inside the square costs one string comparison, and a
        /// step that leaves it pays for the whole ring at once. Interiors have no neighbours and
        /// never cross.
        ///
        /// Everything a crossing implies happens here — the actors come out of the scene before the
        /// walk that would place them twice, the new cells' residents go in, the emitters are warmed
        /// and the sweep runs — so a caller whose crossing `happened` has a scene that may have
        /// grown or been renumbered and must hand it over rather than place it.
        Crossing moveTo(const osg::Vec3f& where);

        /// What the actors and props came to once they were walked in. All zero where there are
        /// none.
        const RtxBridge::ExtractionStats& getSettled() const { return mSettled; }

        /// People and creatures, which is everyone posed less the props.
        std::size_t getActorCount() const;
        std::size_t getPropCount() const;

    private:
        /// Walks the graph into the scene, the way `Tracer` does every frame.
        ///
        /// **Nothing outside calls this any more.** Staging walks once and streaming walks again per
        /// ring; the per-frame walk belongs to `PosedActors`, which owns the pose that made it
        /// necessary.
        RtxBridge::ExtractionStats mirror(std::size_t frame);

        /// Moves the world's clock, which is what everything the graph animates is driven by.
        void setSeconds(float seconds);

        /// The graph the mirror walks, assembled the way the game assembles its own: a group per
        /// cell, a reference under a transform inside it, the terrain hung alongside.
        ///
        /// **Held for as long as the scene is, because the mirror re-walks it every frame.** That
        /// is the whole of what makes this harness measure what the game measures — the walk, the
        /// sweep, and a cell arriving all cost here what they cost there.
        osg::ref_ptr<osg::Group> mRoot = new osg::Group;

        Rtx::SceneDesc mScene;
        RtxBridge::SceneExtractor mExtractor;

        /// Which cells are in the graph, and the group each hangs under.
        LoadedCells mLoaded;

        CellLighting mLighting;
        Placement mPlacement;

        /// Held rather than borrowed: the extractor keys its meshes on node pointers, so actors
        /// freed while the scene still names them is a dangling identity.
        std::unique_ptr<PosedActors> mPosed;
        RtxBridge::ExtractionStats mSettled;

        /// What `moveTo` needs and the constructor already had. Borrowed: the world outlives this.
        World* mWorld = nullptr;
        ActorRequest mActors;

        /// Which square the camera stood in when the region was last brought in. Absent for an
        /// interior, which is the same test as "this never streams".
        std::optional<CellSquare> mStanding;

        /// Where the world's clock stands, in seconds. One clock for the whole staged world: an
        /// actor's idle and the flipbook on the brazier beside them are the same second of it.
        float mSeconds = 0.0f;
    };
}
