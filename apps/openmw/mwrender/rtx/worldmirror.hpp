#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <osg/Matrixd>
#include <osg/Node>
#include <osg/PositionAttitudeTransform>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/contentstore/store.hpp>
#include <components/esm3/refnum.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/image/specularlayout.hpp>
#include <components/rtx/mirror/cells/cellgrid.hpp>
#include <components/rtx/mirror/cells/cellplacer.hpp>
#include <components/rtx/mirror/cells/cellstreamer.hpp>
#include <components/rtx/mirror/cells/cellworld.hpp>
#include <components/rtx/mirror/contentmemory.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/mirror/mirrorpass.hpp>
#include <components/rtx/mirror/pastroom.hpp>
#include <components/rtx/mirror/released.hpp>
#include <components/rtx/mirror/sceneextractor.hpp>
#include <components/rtx/mirror/walkcontext.hpp>
#include <components/rtx/prepare/contentpreparation.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/preprocess/threadcontent.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/renderer/residency.hpp>
#include <components/rtx/renderer/sceneuploader.hpp>
#include <components/rtx/scene/compositequeue.hpp>
#include <components/rtx/scene/ripple.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/terrain/pagedcellref.hpp>

#include "mirrorknobs.hpp"

namespace Resource
{
    class ResourceSystem;
}

namespace MWWorld
{
    class CellStore;
    class GroundcoverStore;
}

namespace SceneUtil
{
    class UnrefQueue;
}

namespace MWRender
{
    struct SceneFrame;
    struct WindowPortalLighting;
    class TracedGroundcover;

    /// The engine's scene graph mirrored into what a ray can meet: everything between "the game has a frame"
    /// and "trace it". Nothing here touches a window, an event, the interface or a benchmark. The maps live
    /// across frames, which makes the mirror incremental: the same crate met again resolves to the mesh
    /// already uploaded, and a cell that left gives its slots back on the frame the sweep misses it.
    class WorldMirror
    {
    public:
        /// @param store the process's content store, or null for none: the frame's walks reach its
        ///        memory alone, and the cell reader its files too.
        /// @param identities the renderer's, which the world's scene takes its identity from, and which outlives it.
        WorldMirror(const MirrorKnobs& knobs, ContentStore::Store* store, Rtx::SceneIdentities& identities);

        /// Asserts the scene empty, so a row a detached world did not give back is named here. Not while an
        /// exception unwinds, where the frame's own message is the one to read.
        ~WorldMirror();

        /// Pinned: its extractor holds its scene and its walk, and its ring its extractor.
        WorldMirror(const WorldMirror&) = delete;
        WorldMirror& operator=(const WorldMirror&) = delete;

        /// The resource system the cell ring's models and the hand-over's pictures are loaded through, and
        /// the queue what the mirror lets go of joins. Told once, where the world is attached; only the image
        /// manager is kept, which is all a frame reaches for.
        void attach(Resource::ResourceSystem& resources, SceneUtil::UnrefQueue& released);

        /// The content's import step, on the loading screen: every model in `models`, each a corrected
        /// path, and every ground layer in `layers`, read on `hands` threads into the store, as the
        /// cell reader reads them (`Rtx::ContentPreparation`). Nothing where there is no store. Once,
        /// before the world.
        void startPreparing(Resource::ResourceSystem& resources, std::vector<std::string> models,
            std::vector<Rtx::PreparationLayer> layers, std::size_t hands);

        /// How far the preparation came, once it is done or `patience` passed: done where none runs.
        Rtx::JobProgress awaitPreparing(std::chrono::milliseconds patience);

        /// Once `awaitPreparing` says it is done: the manifest written, the threads ended, the sea's shape
        /// handed to the store, and what they computed, nothing where none ran. The store is still writing
        /// (`awaitStored`).
        Rtx::ContentStats finishPreparing();

        /// How far the store came with what the preparation handed it, its writes and its stamps, once it is done or
        /// `patience` passed: done where there is no store. **Before the first frame, which asks the store's memory
        /// alone**: an entry still on its way from the disk was computed again. Waited out under the loading screen
        /// and not inside `finishPreparing`, where a day's first start stood undrawn for as long as its disk took to
        /// stamp every entry, 27 s on the handheld.
        Rtx::JobProgress awaitStored(std::chrono::milliseconds patience);

        /// Stops a preparation where it stands, for a player who asked to quit under it: its threads ended, what
        /// they wrote kept in the store, and no manifest written, so the next start walks what this one left.
        void abandonPreparing();

        /// The world is going: every thread that reads it stops and every row it stood is swept, so a
        /// detached world is an empty scene. `SkyReader::detach` gives the sky's own back before this.
        void detach();

        /// The world's groundcover, which the default worldspace's ground is made with
        /// (`RtxRenderer::createGround`): what the ring stands within `[Groundcover] rendering
        /// distance` of the eye. Nothing where `[Groundcover] enabled` is off. Once a world.
        void growGroundcover(const MWWorld::GroundcoverStore& store);

        /// Walks this frame's world into the scene, drops what the walk did not find, and says what the walk
        /// found. The sweep is here and not after the frame, so what `hand` hands over is what this walk met:
        /// a slot the walk stopped finding would otherwise be traced once more where it last stood. The
        /// precipitation goes in as a root of its own because it hangs under a camera-relative transform the
        /// world walk is masked out of, and the sea because this renderer stands it.
        ///
        /// @param view the camera's view matrix as the update traversal settled it, which the
        ///        frame does not carry: `EyeState` says why.
        Rtx::ExtractionStats mirror(const SceneFrame& frame, const osg::Matrixd& view);

        /// What the eye resolves (`Rtx::Sight`), which the next `mirror` tells the ring: the frame's camera is made
        /// after the walk, so the ring asks by the last one, and a frame with none yet asks the finest.
        void setSight(const Rtx::Sight& sight) { mSight = sight; }

        /// What disturbed the water this frame, into the scene the walk just cleared, so the
        /// trace presses it and the digest sees it. After `mirror`, which clears the frame's lists.
        void addRipples(std::span<const Rtx::RippleImpulse> impulses);

        /// Emits area portal lights (`LIGHT_WINDOW`) inward from clustered `mWindowPortal` panes.
        void addWindowPortalLights(const WindowPortalLighting& lighting);

        /// A cell the scene added, which the sea is centred on as `Water::changeCell` centres it
        /// (`seaCentre`): the middle of the cell outdoors, the origin indoors, the last one added winning.
        void standSea(const MWWorld::CellStore& cell);
        /// Hands the world to `scenes` through `residency`, which decides what stands of it, its texels from `feed`
        /// (`Rtx::SceneUploader`).
        Rtx::SceneUpload hand(
            Rtx::SceneHolder& scenes, Rtx::Residency& residency, Rtx::TextureFeed& feed, Rtx::FrameSpend& spend);

        /// How much world this renderer builds, in units: the ground, the air and the distant lights are all
        /// measured over it. The settings' count of cells in the cells of the worldspace the last walk stood
        /// in (`Rtx::CellGrid::reachOf`).
        float getReach() const { return mGrid.reachOf(mKnobs.mReach); }

        /// The worldspace's grid, as the last walk read it off the land.
        const Rtx::CellGrid& getGrid() const { return mGrid; }

        /// The distant cells: the one route to them, for the ground, the frame, a picture asking whether its
        /// ground stands yet, and the harness's checks.
        Rtx::CellStreamer& getRing() { return mRing; }
        const Rtx::CellStreamer& getRing() const { return mRing; }

        /// The menu moved the reach, or the view distance it falls back to. Told rather than read
        /// per frame, so the ring, the air and the map follow one number a frame was handed.
        void setReach(const Rtx::LandReach& reach) { mKnobs.mReach = reach; }

        void setLampSettings(const Rtx::LampSettings& settings) { mKnobs.mLamps = settings; }

        void setInteriorWindowSunlight(bool enabled) { mKnobs.mInteriorWindowSunlight = enabled; }

        /// Where the last walk stood the rings: the camera's eye, which is not the player's feet.
        const osg::Vec3f& getEye() const { return mEye; }

        /// What the eye sees of the world, `Renderer::worldViewMask`: the world walk leaves the player's own
        /// model and the actors out where the mask does, as the rasterizer culls them.
        void setViewMask(std::uint32_t view);

        const Rtx::SceneDesc& getScene() const { return mScene; }
        Rtx::SceneDesc& getScene() { return mScene; }

        /// What the content holds on the host beside the scene — `Rtx::ContentMemory`.
        Rtx::ContentMemory getContentMemory();

        /// `Rtx::SceneExtractor::countPastRoom`, the ring's models with it.
        Rtx::PastRoom countPastRoom() const
        {
            Rtx::PastRoom past = mExtractor.countPastRoom();
            past.mRingModels = mRing.countPastRoom();
            return past;
        }

        /// What every walk on the frame thread shares: the world's, the sky's and every traced view's
        /// (`Rtx::WalkContext`).
        Rtx::WalkContext& getWalkContext() { return mWalk; }

        /// What the world's walk may see. Read by the tests and by nothing else.
        osg::Node::NodeMask getTraversalMask() const { return mTraversal; }

        /// Moves what `released` holds into the queue the world was attached with, so the last reference to
        /// what a walk let go of is dropped on the queue's worker and not on the frame thread. A mirror no
        /// world was attached to, a test's, lets go where it stands.
        void release(Rtx::Released& released);

    private:
        /// The engine's, which outlives every world (`Engine::~Engine` ends it after the world).
        /// Null while no world is attached.
        SceneUtil::UnrefQueue* mReleasing = nullptr;

        /// The process's content store, or null: what the cell reader is handed (`CellWorld::mStore`).
        ContentStore::Store* mStore;

        /// Shared by every walk on the frame thread: the world's and every traced view's.
        Rtx::WalkContext mWalk;

        Rtx::SceneDesc mScene;

        /// Where the ring's models and images come from: the game's own. Made where the world is attached,
        /// when there is a scene manager. Before the ring, whose reader thread reads it.
        std::unique_ptr<Rtx::ContentSource> mContent;

        /// The loading screen's preparation and what it reads, while it runs.
        struct Preparing;
        std::unique_ptr<Preparing> mPreparing;

        /// The most the store ever had left to do under `awaitStored`, which its progress is counted of.
        std::uint32_t mStoringMost = 0;

        /// The world's groundcover, or null where it has none. Before the ring, as the content is.
        std::unique_ptr<TracedGroundcover> mGroundcover;

        /// What the settings say of the mirror: its groundcover, and whether the distance's statics stand, which
        /// every frame's world says (`Rtx::CellWorld::mStatics`), and its reach, which a change moves (`setReach`).
        MirrorKnobs mKnobs;

        Rtx::SceneExtractor mExtractor;

        /// What `setViewMask` last let the walk see: every class until then, as a fresh seam's view
        /// mask has it.
        osg::Node::NodeMask mTraversal;

        /// The sea: upstream's water geometry under `Mask_Water`, which is how the extractor
        /// knows a sea from a floor, stood at the frame's water height and hidden where the frame
        /// says there is none. Made once; a frame moves it.
        osg::ref_ptr<osg::PositionAttitudeTransform> mSea;
        osg::Vec2f mSeaCentre;

        /// The cells themselves: their ground off the land records, their statics as instances
        /// of their templates, and their lamps. After the scene, which it adopts into.
        Rtx::CellStreamer mRing{ mExtractor };

        Rtx::SceneUploader mUploader;

        /// The distant cells waiting for their ground to be flattened, in the order the walks asked. Here
        /// because only a world has ground and a chunk waits past the frame that asked for it.
        Rtx::CompositeQueue mComposites;
        Rtx::CellGrid mGrid;
        osg::Vec3f mEye;
        Rtx::Sight mSight{};
    };
}
