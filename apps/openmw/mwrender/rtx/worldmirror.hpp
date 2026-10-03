#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <osg/Matrixd>
#include <osg/Node>
#include <osg/PositionAttitudeTransform>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/esm3/refnum.hpp>
#include <components/rtx/mirror/cells/cellgrid.hpp>
#include <components/rtx/mirror/cells/cellplacer.hpp>
#include <components/rtx/mirror/cells/cellring.hpp>
#include <components/rtx/mirror/cells/cellworld.hpp>
#include <components/rtx/mirror/cells/mirrorknobs.hpp>
#include <components/rtx/mirror/contentmemory.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/mirror/mirrorpass.hpp>
#include <components/rtx/mirror/sceneextractor.hpp>
#include <components/rtx/mirror/walkcontext.hpp>
#include <components/rtx/preprocess/threadcontent.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/renderer/sceneuploader.hpp>
#include <components/rtx/scene/compositequeue.hpp>
#include <components/rtx/scene/ripple.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/terrain/pagedcellref.hpp>

namespace Resource
{
    class ResourceSystem;
}

namespace MWWorld
{
    class CellStore;
    class GroundcoverStore;
}

namespace Rtx
{
    class Renderer;
}

namespace MWRender
{
    struct SceneFrame;
    class TracedGroundcover;

    /// The engine's scene graph mirrored into what a ray can meet.
    ///
    /// **Everything between "the game has a frame" and "trace it".** The walk, what it walks past,
    /// what this renderer stands for the ground and the distance, the lights the game has already
    /// placed, and the hand-over that decides whether the device is placed, extended or rebuilt.
    /// Nothing here touches a window, an event, the interface or a benchmark.
    ///
    /// **The maps live across frames**, which is what makes the mirror incremental: the same crate
    /// met again resolves to the mesh already uploaded rather than to a copy of it, and a cell that
    /// left gives its slots back on the frame the sweep misses it.
    class WorldMirror
    {
    public:
        explicit WorldMirror(const Rtx::MirrorKnobs& knobs);

        /// Asserts the scene empty: a world detached has given every row back, and the one that
        /// did not is named here rather than found at the next frame's horizon. Not while an
        /// exception unwinds, where the frame's own message is the one to read.
        ~WorldMirror();

        /// The resource system the cell ring's models and the hand-over's pictures are loaded
        /// through. Told once, where the world is attached; what is kept of it is the image
        /// manager, which is all a frame reaches for.
        void attach(Resource::ResourceSystem& resources);

        /// The world is going: every thread that reads it stops, what was read of it goes, and
        /// every row the world stood is swept — so a detached world is an empty scene, which the
        /// destructor asserts. `SkyReader::detach` gives the sky's own back before this.
        void detach();

        /// The world's groundcover, which the default worldspace's ground is made with
        /// (`RtxRenderer::createGround`): what the ring stands within `[Groundcover] rendering
        /// distance` of the eye. Nothing where `[Groundcover] enabled` is off. Once a world.
        void growGroundcover(const MWWorld::GroundcoverStore& store);

        /// Walks this frame's world into the scene, drops what the walk did not find, and says
        /// what the walk found. The sweep is here and not after the frame, so what `hand` hands
        /// over is what this walk met: a slot the walk stopped finding — a crate picked up, a
        /// body whose identity moved — would otherwise be traced once more where it last stood.
        ///
        /// The precipitation and the sea go in as roots of their own: the precipitation because it
        /// hangs under a camera-relative transform the world walk is masked out of, the sea because
        /// this renderer stands it — the plane the rasterizer's `Water` stood was the sea a ray met,
        /// and that object is the rasterizer's now.
        ///
        /// @param view the camera's view matrix as the update traversal settled it, which the
        ///        frame does not carry: `EyeState` says why.
        Rtx::ExtractionStats mirror(const SceneFrame& frame, const osg::Matrixd& view);

        /// What disturbed the water this frame, into the scene the walk just cleared, so the
        /// trace presses it and the digest sees it. After `mirror`, which clears the frame's lists.
        void addRipples(std::span<const Rtx::RippleImpulse> impulses);

        /// A cell the scene added, which is what the sea is centred on, as `Water::changeCell`
        /// centres it (`seaCentre`): the middle of the cell outdoors, the origin indoors, the last
        /// one added winning.
        void standSea(const MWWorld::CellStore& cell);

        /// Hands the scene to `renderer`, building only what has to be built, and then ends the
        /// placement: where everything stands is what the next frame measures its motion against,
        /// and the change lists the backend just took start again.
        Rtx::SceneUpload hand(Rtx::Renderer& renderer, Rtx::FrameSpend& spend);

        /// How much world this renderer builds, in units: the ground, the air and the distant
        /// lights are all measured over it. The settings' count of cells, as they stood when the
        /// mirror was made or when the menu last moved them, in the cells of the worldspace the
        /// last walk stood in (`Rtx::CellGrid::reachOf`).
        float getReach() const { return mGrid.reachOf(mReach); }

        /// The worldspace's grid, as the last walk read it off the land.
        const Rtx::CellGrid& getGrid() const { return mGrid; }

        /// The distant cells: the one route to them, for the ground that tells them what the game
        /// says of a reference, the frame that asks how much is left to stand, a picture that asks
        /// whether its ground stands yet, and the harness's checks.
        Rtx::CellRing& getRing() { return mRing; }
        const Rtx::CellRing& getRing() const { return mRing; }

        /// The menu moved the reach, or the view distance it falls back to. Told rather than read
        /// per frame, so the ring, the air and the map follow one number a frame was handed.
        void setReach(const Rtx::LandReach& reach) { mReach = reach; }

        /// Where the last walk stood the rings: the camera's eye, which is not the player's feet.
        const osg::Vec3f& getEye() const { return mEye; }

        /// What the eye sees of the world, `Renderer::worldViewMask`: the world walk leaves the
        /// player's own model and the actors out where the mask does, as the rasterizer culls
        /// them. The game keeps `Mask_Player` in, a static camera a script parks included; a host
        /// whose camera stands inside the player takes it out, and `tws` takes the actors out.
        void setViewMask(unsigned int view);

        const Rtx::SceneDesc& getScene() const { return mScene; }
        Rtx::SceneDesc& getScene() { return mScene; }

        /// What the content holds on the host beside the scene — `Rtx::ContentMemory`.
        Rtx::ContentMemory getContentMemory();

        /// What every walk on the frame thread shares — the world's, the sky's and every traced
        /// view's: the traversal numbers, what the walks compute from the content, and what its
        /// `_spec` maps mean (`Rtx::WalkContext`).
        Rtx::WalkContext& getWalkContext() { return mWalk; }

        /// What the world's walk may see. Read by the tests and by nothing else.
        osg::Node::NodeMask getTraversalMask() const { return mTraversal; }

    private:
        /// Shared by every walk on the frame thread: the world's and every traced view's.
        Rtx::WalkContext mWalk;

        Rtx::SceneDesc mScene;

        /// Where the ring's models and images come from: the game's own. Made where the world is
        /// attached, because that is when there is a scene manager. Before the ring, whose reader
        /// thread reads it: the members below die first, and the thread with them.
        std::unique_ptr<Rtx::ContentSource> mContent;

        /// The world's groundcover, or null where it has none. Before the ring, as the content is.
        std::unique_ptr<TracedGroundcover> mGroundcover;
        float mGroundcoverReach = 0.0f;
        float mGroundcoverDensity = 0.0f;
        bool mGroundcoverLampLit = true;

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
        Rtx::CellRing mRing{ mExtractor };

        Rtx::SceneUploader mUploader;

        /// The distant cells waiting for their ground to be flattened, in the order the walks asked.
        ///
        /// **Here because only a world has ground.** A chunk waits past the frame that asked for
        /// it, so the schedule belongs to what outlives frames rather than to the once-a-frame
        /// call — and every picture inside the interface goes through that same call with no
        /// ground to flatten.
        Rtx::CompositeQueue mComposites;

        Rtx::LandReach mReach;
        Rtx::CellGrid mGrid;
        osg::Vec3f mEye;
    };
}
