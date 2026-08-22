#pragma once

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

    /// What has to be decided before a region can be read, beyond which cell it is.
    struct StagingRequest
    {
        /// How many cells out from the one asked for to read, so four is nine by nine. An interior
        /// ignores it.

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
    /// **The window does not use this.** Its camera goes somewhere, so it has to bring the next
    /// ring of cells in and re-take its snapshot as it flies; what it needs is this plus a way to
    /// keep doing it, which is `runWindow`'s own business.
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
        const CellLighting& getLighting() const { return mLighting; }
        const Placement& getPlacement() const { return mPlacement; }

        /// What advances the scene between frames, or null where nothing in it moves. Borrowed:
        /// it does not outlive this.
        Motion* getMotion() { return mPosed.get(); }

        /// The graph the mirror walks. Borrowed for the same reason.
        osg::Group& getRoot() { return *mRoot; }

        /// Walks the graph into the scene, the way `Tracer` does every frame.
        RtxBridge::ExtractionStats mirror(std::size_t frame);

        /// What the actors and props came to once they were walked in. All zero where there are
        /// none.
        const RtxBridge::ExtractionStats& getSettled() const { return mSettled; }

        /// People and creatures, which is everyone posed less the props.
        std::size_t getActorCount() const;
        std::size_t getPropCount() const;

    private:
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
    };
}
