#include "worldmirror.hpp"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <osg/Geometry>
#include <osg/Image>
#include <osg/Matrixd>
#include <osg/Matrixf>
#include <osg/Node>
#include <osg/PositionAttitudeTransform>
#include <osg/Referenced>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <apps/openmw/mwrender/sceneframe.hpp>
#include <apps/openmw/mwrender/searules.hpp>
#include <apps/openmw/mwrender/sky.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <apps/openmw/mwworld/cell.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/weather.hpp>
#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/misc/result.hpp>
#include <components/nifosg/nifloader.hpp>
#include <components/platform/heap.hpp>
#include <components/platform/memory.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/rtx/common/mebibytes.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/mirror/cells/cellgrid.hpp>
#include <components/rtx/mirror/cells/cellworld.hpp>
#include <components/rtx/mirror/cells/nightday.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/mirror/meshreader.hpp>
#include <components/rtx/mirror/nodekind.hpp>
#include <components/rtx/prepare/contentpreparation.hpp>
#include <components/rtx/prepare/filestamps.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/meshtable.hpp>
#include <components/sceneutil/unrefqueue.hpp>
#include <components/terrain/storage.hpp>
#include <components/terrain/world.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vfs/recursivedirectoryiterator.hpp>

#include "classmasks.hpp"
#include "mirrorknobs.hpp"
#include "tracedgroundcover.hpp"

namespace MWRender
{
    namespace
    {
        /// The game's own models and images, as `Rtx::CellReader` asks for them.
        class SceneContent final : public Rtx::ContentSource
        {
        public:
            explicit SceneContent(Resource::SceneManager& scenes)
                : mScenes(scenes)
            {
            }

            osg::ref_ptr<const osg::Node> getTemplate(VFS::Path::NormalizedView path) override
            {
                // Uncompiled, because nothing here has a context to compile for: `compile` queues
                // the model's OpenGL objects, and this renderer initialises no OpenGL.
                return mScenes.getTemplate(path, false);
            }

            Result<osg::ref_ptr<const osg::Image>, std::string> getImage(VFS::Path::NormalizedView path) override
            {
                return Rtx::openImage(*mScenes.getImageManager(), path);
            }

            void letGo(std::span<const std::string> paths) override { mScenes.letGo(paths); }

        private:
            Resource::SceneManager& mScenes;
        };

        /// A file's stamp as the virtual file system holds it: the archive or folder that holds it and
        /// its time, so a file a later folder shadows, an archive rebuilt and a loose file written each
        /// move it (`Rtx::FileStamps`).
        class VfsStamps final : public Rtx::FileStamps
        {
        public:
            explicit VfsStamps(const VFS::Manager& vfs)
                : mVfs(vfs)
            {
            }

            std::uint64_t stampOf(VFS::Path::NormalizedView path) override
            {
                if (!mVfs.exists(path))
                    return 0;
                const std::string archive = mVfs.getArchive(VFS::Path::Normalized(path));
                const auto time = mVfs.getLastModified(path).time_since_epoch().count();
                ContentStore::Digest digest("stamp", 1);
                digest.add(std::span<const char>(archive));
                digest.add(time);
                return digest.getKey().mHash[0];
            }

        private:
            const VFS::Manager& mVfs;
        };

        /// Which child the ring's day-night switches show: the game's mode, whose value is the
        /// child's index as the ring's is, or the child each file opens on where the game drives
        /// no switch.
        Rtx::NightDayMode nightDayOf(const WorldState& world)
        {
            static_assert(static_cast<unsigned int>(Rtx::NightDayMode::Default) == MWWorld::Default
                && static_cast<unsigned int>(Rtx::NightDayMode::ExteriorNight) == MWWorld::ExteriorNight
                && static_cast<unsigned int>(Rtx::NightDayMode::InteriorDay) == MWWorld::InteriorDay);

            if (!world.mNightDayMode.has_value())
                return Rtx::NightDayMode::Authored;

            assert(*world.mNightDayMode <= MWWorld::InteriorDay && "a day-night mode the ring has no name for");
            return static_cast<Rtx::NightDayMode>(*world.mNightDayMode);
        }

        /// What every walk this renderer makes takes: an exclusion of what the ray tracer draws
        /// itself, never a selection of what a walk is interested in. A node mask is AND-ed at
        /// every node, so naming `Mask_WeatherParticles` to mean "the weather subtree" extracted
        /// every storm with its particles missing, because a blizzard's own particles are marked
        /// `Mask_ParticleSystem`. Which subtree is walked is answered by where the walk starts.
        /// The ground is the ring's: what `TracedTerrain` stands under `Mask_Terrain` is the
        /// intersector's, and walked it would place every loaded cell's ground a second time.
        ///
        /// **`Mask_UpdateVisitor` is what the content hides with, and it is named here rather than
        /// asked of `NifOsg::Loader`.** The loader is told that bit by `RenderingManager`, and this
        /// renderer is built before there is a rendering manager to tell it, so a mask that reads
        /// the loader's answer subtracts nought: every node a `NifOsg::VisController` hides is
        /// traced anyway, and the Heart of Lorkhan stands wearing the whole of its destruction at
        /// the frame that sequence opens on. The bit is the engine's own decision and a constant,
        /// so it is stated as one and no order can reach it — which is what
        /// `MWRender::ObjectPaging` does with the same bit for the same reason.
        /// `WorldMirror::mirror` checks the loader against it.
        constexpr osg::Node::NodeMask sWorldTraversal = ~static_cast<osg::Node::NodeMask>(
            Mask_Sky | Mask_Sun | Mask_SimpleWater | Mask_Terrain | Mask_UpdateVisitor);

        /// What each walk this renderer makes over the one extractor is anchored at. Four roots the
        /// walk cannot tell apart by structure, so each is named; the world's is nought, which is
        /// what `SceneExtractor::extract` calls a caller that walks one whole graph.
        enum Anchor : std::size_t
        {
            World = 0,
            Sea = 1,
            Rain = 2,
            Effect = 3,
        };

        /// What the world walk may see: `sWorldTraversal`, without the player or the actors where the
        /// eye's view mask leaves them out — `WorldMirror::setViewMask`. Those two only, because they
        /// share one class: the trace's ray mask hides every other class the view leaves out, and
        /// a walk that went on past an actor would stand it in the class the player keeps on.
        constexpr osg::Node::NodeMask worldTraversal(const unsigned int view)
        {
            constexpr osg::Node::NodeMask walkedByView = Mask_Player | Mask_Actor;

            return sWorldTraversal & ~(walkedByView & ~view);
        }
    }

    WorldMirror::WorldMirror(
        const MirrorKnobs& knobs, ContentStore::Store* const store, Rtx::PreparedImages* const prepared)
        : mStore(store)
        , mWalk{ .mContent{ store, ContentStore::Reach::Memory }, .mPrepared = prepared }
        , mGroundcoverReach(knobs.mGroundcoverReach)
        , mGroundcoverDensity(knobs.mGroundcoverDensity)
        , mGroundcoverLampLit(knobs.mGroundcoverLampLit)
        , mDistantStatics(knobs.mDistantStatics)
        , mExtractor(mScene, mWalk, Rtx::sWorldExtractorRoom)
        , mTraversal(worldTraversal(~0u))
        , mReach(knobs.mReach)
    {
        mRing.setMinSize(knobs.mMinSize);
        // The sky is not mirrored: the engine rebuilds it every frame, state sets and all, so
        // walking it churns the identity maps and makes every frame a full rebuild, and a ray that
        // reaches the sky gets this renderer's own. The simple water is the local map's copy of the
        // sea, which a mirror walking both would place twice. What the content hides is the one bit
        // `sWorldTraversal` names, rather than none, so the update traversal still reaches a hidden
        // bone.
        mExtractor.setTraversalMask(mTraversal);

        // Where the engine stamps its identities: the cell roots under the scene root and the
        // reference roots under those, and the player beside the cells (`MWRender::Objects`).
        mExtractor.setStampDepth(2);

        // What is left of the two is the sea, which this renderer stands: upstream's plane, as
        // `MWRender::Water` makes it, on a transform a frame moves.
        mExtractor.setWaterMask(Mask_Water);

        osg::ref_ptr<osg::Geometry> sea = createSeaGeometry();
        sea->setNodeMask(Mask_Water);
        sea->setName("Sea Geometry");
        mSea = new osg::PositionAttitudeTransform;
        mSea->setName("Sea Root");
        mSea->addChild(sea);

        // The roots the game marks, so a camera's cull mask can keep or leave out what stands
        // under them — `describeView` reads the same table the other way.
        for (const ClassMask& held : sClassMasks)
            if (held.mClass != Rtx::InstanceClass::Static)
                mExtractor.setClassMask(held.mClass, held.mNodes);
    }

    namespace
    {
        /// What the loading screen keeps beside the preparation, out of the memory the system leaves
        /// the process (`Platform::availableMemory`): the store's queue of writes, 64 MiB at most, and the
        /// screen's own growth past the preparation's start, 51 MB on the Fullrest Repack with the
        /// kernels compiled before (`docs/rtx/research/2026-11-09-content-preparation-cost.md`, "The
        /// reserve"), with the rest of the gigabyte for a compile still running. What comes after the
        /// preparation is not reserved: the preparation lets go of everything before the world loads.
        constexpr std::uint64_t sPreparationReserve = std::uint64_t{ 1 } << 30;

        /// The bytes the preparation's jobs may hold together, read once at its start, or nothing where
        /// the system states no memory.
        std::optional<std::uint64_t> preparationBudget()
        {
            const std::optional<std::uint64_t> available = Platform::availableMemory();
            if (!available.has_value())
                return std::nullopt;
            return *available - std::min(*available, sPreparationReserve);
        }

        /// What reading `sea` through the disk cost: its shape found in `store` or folded and filed there,
        /// and held in the store's memory either way. As the frame's walk reads it, standing in no model.
        Rtx::ContentStats prepareSea(ContentStore::Store& store, const osg::Geometry& sea)
        {
            Rtx::ContentPreprocessor content(&store, ContentStore::Reach::Disk);
            Rtx::MeshReader reader;
            Rtx::MeshReading reading;
            [[maybe_unused]] const Result<bool, std::string> read
                = reader.read(content, Rtx::readDrawable(sea, Rtx::NodeKind::Other), nullptr, reading);
            assert(read.isOk() && "the sea's own plane does not read");
            return content.takeStats();
        }
    }

    /// The preparation and what it reads, which outlive it: made together, gone together. Pinned:
    /// the run holds its sources by reference.
    struct WorldMirror::Preparing
    {
        Preparing(Resource::ResourceSystem& resources, ContentStore::Store& store, std::vector<std::string> models,
            std::vector<Rtx::PreparationLayer> layers, const std::size_t hands)
            : mContent(*resources.getSceneManager())
            , mStamps(*resources.getVFS())
            // With the world's own mask and with no state set above them: the scene's root is the
            // rendering manager's, which stands after the loading screen, and what stands above a
            // part keys its material, not a pass.
            , mRun(Rtx::PreparationSetup{ .mContent = mContent,
                       .mStamps = mStamps,
                       .mStore = store,
                       .mManifest = store.getDirectory() / "prepared.manifest",
                       .mMask = sWorldTraversal,
                       .mAutoMaps = resources.getSceneManager()->getAutoMaps(),
                       .mHands = hands,
                       .mBudget = preparationBudget(),
                       .mDay = static_cast<std::uint32_t>(
                           std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())
                               .time_since_epoch()
                               .count()) },
                  std::move(models), std::move(layers))
        {
        }

        SceneContent mContent;
        VfsStamps mStamps;
        Rtx::ContentPreparation mRun;
    };

    void WorldMirror::startPreparing(Resource::ResourceSystem& resources, std::vector<std::string> models,
        std::vector<Rtx::PreparationLayer> layers, const std::size_t hands)
    {
        assert(mPreparing == nullptr && "a preparation started over one that runs");
        if (mStore != nullptr)
            mPreparing = std::make_unique<Preparing>(resources, *mStore, std::move(models), std::move(layers), hands);
    }

    Rtx::JobProgress WorldMirror::awaitPreparing(const std::chrono::milliseconds patience)
    {
        return mPreparing != nullptr ? mPreparing->mRun.awaitProgress(patience) : Rtx::JobProgress{};
    }

    Rtx::ContentStats WorldMirror::finishPreparing()
    {
        if (mPreparing == nullptr)
            return {};

        if (!mPreparing->mRun.finish())
            Log(Debug::Warning) << "The content's preparation could not write its manifest";

        // **The sea as well, which no file holds and so no model's walk reads**: the frame's thread, whose
        // asks reach the store's memory alone, folded it again every session.
        Rtx::ContentStats stats = mPreparing->mRun.getStats();
        stats += prepareSea(*mStore, *mSea->getChild(0)->asGeometry());

        // **The store's start swept and loaded before the first frame asks its memory**, which the frame
        // reaches alone: an entry still on its way from the disk was one the frame computed again.
        mStore->flush();

        // **What it took, said once**, in the game as under every verb: the wall against the threads'
        // CPU says whether they waited, and the passes say on what the CPU went.
        const Rtx::PreparationTimes times = mPreparing->mRun.getTimes();
        using Seconds = std::chrono::duration<double>;
        Log(Debug::Info) << std::format(
            "Prepared {} of {} models and ground layers in {:.2f} s, {:.1f} s of CPU on {} threads; the rest "
            "stood as the last preparation found them",
            mPreparing->mRun.getWalked(), mPreparing->mRun.awaitProgress(std::chrono::milliseconds::zero()).mCount,
            Seconds(times.mWall).count(), Seconds(times.mThreadsCpu).count(), times.mThreads);
        if (times.mBudget.has_value())
            Log(Debug::Info) << std::format("  its jobs held at most {:.0f} MiB of a budget of {:.0f} MiB",
                Rtx::mebibytes(times.mHeldMost), Rtx::mebibytes(*times.mBudget));
        else
            Log(Debug::Info) << std::format("  its jobs held at most {:.0f} MiB, with no budget the system states",
                Rtx::mebibytes(times.mHeldMost));
        for (const auto& [pass, name] : Rtx::sContentPasses.mNames)
        {
            const Rtx::PassStats& counted = stats.at(pass);
            if (counted.mAsked > 0)
                Log(Debug::Info) << std::format(
                    "  {:<12} {:>7} asked, {:>7} found, {:>10.1f} ms keying, {:>10.1f} ms reading found, "
                    "{:>10.1f} ms running",
                    name, counted.mAsked, counted.mHits, counted.mKeyMs, counted.mFoundMs, counted.mRunMs);
        }
        mPreparing.reset();

        // **Its threads' freed memory back to the system, once** (`Platform::trimHeap`): their chain
        // builds and models are gone with it, and glibc keeps what an arena freed until a trim.
        Platform::trimHeap();
        return stats;
    }

    void WorldMirror::attach(Resource::ResourceSystem& resources, SceneUtil::UnrefQueue& released)
    {
        mContent = std::make_unique<SceneContent>(*resources.getSceneManager());
        mReleasing = &released;

        // Every texture the content holds, so the frame's cache of their facts never rehashes on
        // the frame that meets one more: one walk of the directory, at load.
        std::size_t textures = 0;
        for ([[maybe_unused]] const VFS::Path::Normalized& file :
            resources.getVFS()->getRecursiveDirectoryIterator(VFS::Path::NormalizedView("textures/")))
            ++textures;
        mWalk.mContent.mFacts.reserve(textures);
    }

    void WorldMirror::release(Rtx::Released& released)
    {
        if (mReleasing == nullptr)
        {
            released.clear();
            return;
        }

        // One handover for the whole batch, so a frame that lets go of many objects allocates the
        // batch and its share alone.
        if (!released.isEmpty())
            mReleasing->push(std::make_shared<std::vector<osg::ref_ptr<const osg::Referenced>>>(released.take()));
    }

    WorldMirror::~WorldMirror()
    {
        assert((std::uncaught_exceptions() > 0 || mScene.isEmpty()) && "a world detached and still standing rows");
    }

    void WorldMirror::detach()
    {
        // **The ring's thread reads the storages the world owns.** A world with nothing in it is
        // what stops the thread and drops what it held.
        mRing.follow(Rtx::WorldAround{});

        // What the ring held on the extractor's rows goes back, and the sweep frees them with
        // everything the walks stood: nothing of this world stays in the scene. Not on the way
        // out of an exception a frame threw, where the scene is whatever the throw left and the
        // assert in the retire would stand between the throw and its message.
        if (std::uncaught_exceptions() == 0)
        {
            mExtractor.detach(mRing);
            release(mExtractor.getReleased());
        }
        mReleasing = nullptr;

        mGroundcover.reset();
        mContent.reset();
    }

    void WorldMirror::growGroundcover(const MWWorld::GroundcoverStore& store)
    {
        assert(mGroundcover == nullptr && "the default worldspace's ground made twice for one world");

        if (mGroundcoverReach > 0.0f)
            mGroundcover = std::make_unique<TracedGroundcover>(store, mGroundcoverDensity, mGroundcoverLampLit);
    }

    Rtx::ContentMemory WorldMirror::getContentMemory()
    {
        const Rtx::MeshTable& meshes = mScene.meshes();
        const Rtx::DeformerTable& deformers = mScene.deformers();
        const auto count = [](const auto& run) { return static_cast<std::uint32_t>(run.size()); };
        return Rtx::ContentMemory{
            .mVertexEnd = meshes.getVertexRuns().getEnd(),
            .mVerticesUsed = meshes.getVertexRuns().getUsed(),
            .mIndexEnd = meshes.getIndexRuns().getEnd(),
            .mIndicesUsed = meshes.getIndexRuns().getUsed(),
            .mLayerEnd = count(mScene.materials().getLayers()),
            .mMaskEnd = count(mScene.materials().getMasks()),
            .mRigRunEnd = count(deformers.getRuns()),
            .mInfluenceEnd = count(deformers.getInfluences()),
            .mOffsetEnd = count(deformers.getMorphOffsets()),
            .mPoseEnd = count(deformers.getPoses()),
            .mBindVertexEnd = deformers.getBindVertexCount(),
            .mReader = mRing.getReaderMemory(),
        };
    }

    void WorldMirror::standSea(const MWWorld::CellStore& cell)
    {
        const MWWorld::Cell& stood = *cell.getCell();
        mSeaCentre = stood.isExterior() ? seaCentre(stood.getGridX(), stood.getGridY()) : osg::Vec2f(0.f, 0.f);
    }

    void WorldMirror::setViewMask(const unsigned int view)
    {
        mTraversal = worldTraversal(view);
        mExtractor.setTraversalMask(mTraversal);
    }

    Rtx::ExtractionStats WorldMirror::mirror(const SceneFrame& frame, const osg::Matrixd& view)
    {
        const std::size_t frameNumber = frame.mWhen.getFrameNumber();

        // **Whatever the loader hides with has to be inside what this mask excludes.**
        // `sWorldTraversal` names that bit rather than asking, because the loader is told it by
        // `RenderingManager` and this renderer is built first — so the two are checked against each
        // other here instead, where a walk is about to use the mask. A loader nobody has told yet
        // hides with nothing and passes: what this catches is the engine moving the bit.
        assert((NifOsg::Loader::getHiddenNodeMask() & ~static_cast<unsigned int>(Mask_UpdateVisitor)) == 0
            && "the loader hides with a bit the walk's mask does not exclude");

        // The world's clock and not this renderer's, or the controllers would run while the game
        // was paused.
        mExtractor.setSimulationTime(
            frame.mWhen.getSimulationTime(), frame.mPaused ? 0.0 : static_cast<double>(frame.mDeltaTime));

        // What goes is the lists a walk refills wholesale; the meshes, materials and textures stay
        // because the structures were built from them, and the placements because they are
        // addressed by slot.
        mScene.clearPlacement();

        // What the weather drops, walked as a second root, because the sky's mask keeps the world
        // walk out of that subtree: the same systems the rasterizer draws, stood at the eye.
        const osg::Matrixd inverseView = osg::Matrixd::inverse(view);
        const osg::Vec3f eye = inverseView.getTrans();
        mEye = eye;

        // And the eye every billboard in the world turns to, which the rasterizer's cull hands its
        // `AutoTransform`s and this walk has to be told.
        mExtractor.setEye(Rtx::viewBasisOf(inverseView));
        mExtractor.extractPrecipitation(frame.mPrecipitation.getRainNode(), eye, Anchor::Rain, frameNumber);
        mExtractor.extractPrecipitation(frame.mPrecipitation.getParticleNode(), eye, Anchor::Effect, frameNumber);

        // The sea, where the frame says there is one: hidden by its mask otherwise, as the
        // rasterizer's `updateVisible` hid the same plane, so the walk leaves no placement of it.
        mSea->setPosition(osg::Vec3f(mSeaCentre.x(), mSeaCentre.y(), frame.mWorld.mWater.mHeight));
        mSea->setNodeMask(frame.mWorld.mWater.isShown() ? ~0u : 0u);
        mExtractor.extract(*mSea, osg::Matrixf::identity(), Anchor::Sea, frameNumber);

        // The eye, the reach, the world's own grid, the hour and the day-night mode, said once to
        // the ring: what the game has stood for itself is what the ring may not stand again, and
        // its lamps burn and its windows light at the world's clock as the graph's do.
        Terrain::Storage* const ground = frame.mTerrain.getStorage();
        const ESM::RefId worldspace = frame.mTerrain.getWorldspace();
        mGrid = ground != nullptr ? Rtx::CellGrid(ground->getCellWorldSize(worldspace)) : Rtx::CellGrid();

        const Rtx::WorldAround around{
            .mWorld = {
                .mStorage = &frame.mObjectStorage,
                .mGround = ground,
                .mContent = mContent.get(),
                // The rasterizer grows grass in the default worldspace alone (`GlRenderer::createGround`).
                .mGroundcover = worldspace == ESM::Cell::sDefaultWorldspaceId ? mGroundcover.get() : nullptr,
                .mWorldspace = worldspace,
                .mGrid = mGrid,
                // The world's mask and never the walk's own: a mask that moves makes
                // `Rtx::CellRing::forget` read the ring from nothing, and the player bit moves
                // while the camera settles.
                .mMask = sWorldTraversal,
                // What `extractWorld` walks every reference root under, below.
                .mAbove = frame.mScene.getStateSet(),
                .mStatics = mDistantStatics,
                .mStore = mStore,
            },
            .mEye = eye,
            .mReach = getReach(),
            .mGroundcoverReach = mGroundcoverReach,
            .mActiveGrid = frame.mTerrain.getActiveGrid(),
            .mExterior = frame.mWorld.mLocation == Location::Exterior,
            .mSimulationTime = frame.mWhen.getSimulationTime(),
            .mNightDay = nightDayOf(frame.mWorld),
        };

        // Told once a frame, because what the graph does not hold is the frame's to say. The
        // world walk asks it, and the precipitation walk above cannot: it is a subtree. Which
        // frame the ring is standing for is `extractWorld`'s to say, because that call has the
        // ring and the number both.
        mRing.follow(around);

        // One walk over the whole graph, where every path is already distinct, and the one that
        // stands the references the game said jumped: the other walks stand nothing of theirs.
        mExtractor.setJumped(frame.mJumped);
        Rtx::ExtractionStats found = mExtractor.extractWorld(frame.mScene, Anchor::World, frameNumber, mRing);
        mExtractor.setJumped({});

        // What the walks did not find has gone. The graph is the whole world every frame, which is
        // what makes mark and sweep sound; the identity maps hold their keys alive until it runs.
        // After every walk of the frame and never before one, because the sweep bumps the epoch
        // the next walk is measured against.
        mExtractor.retire();
        release(mExtractor.getReleased());

        // **Taken once, after every walk of the frame**, and not by a walk: the precipitation and
        // the sea are walks whose counts go nowhere, and the sky's sheets are read between walks,
        // so a count a walk took with it was a count the frame lost.
        found.mPreprocessed.mOnFrame += mWalk.mContent.mPreprocessor.takeStats();

        return found;
    }

    void WorldMirror::addRipples(std::span<const Rtx::RippleImpulse> impulses)
    {
        for (const Rtx::RippleImpulse& impulse : impulses)
            mScene.addRipple(impulse);
    }

    Rtx::SceneUpload WorldMirror::hand(Rtx::SceneHolder& scenes, Rtx::FrameSpend& spend)
    {
        return mUploader.hand(scenes,
            Rtx::SceneUploader::Handing{ .mSlot = Rtx::SceneSlot::world(),
                .mScene = mScene,
                .mComposites = &mComposites,
                .mSpend = &spend,
                .mPrepared = mWalk.mPrepared,
                .mSettled = mRing.isSettled() });
    }

}
