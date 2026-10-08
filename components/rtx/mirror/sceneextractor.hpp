#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>
#include <osg/Matrix>
#include <osg/Matrixf>
#include <osg/Node>
#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/rtx/common/runs.hpp>
#include <components/rtx/common/stepped.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/mirror/lightbuilder.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/specularlayout.hpp>

#include "chainkeys.hpp"
#include "emitterresolver.hpp"
#include "extractionstats.hpp"
#include "materialresolver.hpp"
#include "meshreader.hpp"
#include "meshresolver.hpp"
#include "mirroridentity.hpp"
#include "mirrorpass.hpp"
#include "nodekind.hpp"
#include "released.hpp"
#include "sceneadopter.hpp"
#include "shading.hpp"
#include "walkcontext.hpp"

namespace osg
{
    class Drawable;
    class Geometry;
    class Image;
    class StateSet;
}

namespace osgParticle
{
    class ParticleSystem;
}

namespace SceneUtil
{
    class LightSource;
    class MorphGeometry;
    class StateSetUpdater;
}

namespace Rtx
{
    class CellRing;

    /// Mirrors an OpenSceneGraph subtree into a `SceneDesc`. The identity maps live across calls,
    /// so the same geometry met again resolves to the mesh already uploaded rather than to a copy,
    /// and the mirror is incremental instead of a rebuild per frame.
    class SceneExtractor : public SceneAdopter
    {
    public:
        /// @param context what every walk on this thread shares — `WalkContext`. The game hands the
        ///        frame thread's to the world's walk and to every traced view's.
        SceneExtractor(SceneDesc& scene, WalkContext& context);

        /// Gives back every hold the maps took on the scene — every placement, every row, every
        /// texture — so a scene that outlives this holds nothing of it. The scene outlives it by
        /// the reference this keeps.
        ~SceneExtractor();

        /// Which nodes the walks may descend into, as an `osg` traversal mask: what keeps the
        /// mirror out of subtrees the ray tracer answers for itself, such as the sky's mask,
        /// without knowing what a sky is, and out of what the content hides.
        ///
        /// **Every node, until an owner states less.** The bit a hidden node carries is the
        /// engine's own and this component cannot name it, so a default that subtracted it had to
        /// ask `NifOsg::Loader` at the moment the extractor was built — an answer that depends on
        /// whether anything has told the loader yet, and in the game nothing has. A default that is
        /// plainly everything is wrong where it shows rather than wrong where it does not.
        void setTraversalMask(osg::Node::NodeMask mask) { mTraversalMask = mask; }

        /// Which nodes are the world's water, as an `osg` node mask. None by default. Water reaches
        /// here as an ordinary blended quad, so without this it is shaded as a painted surface:
        /// every shallow goes black under a shadow ray and there are no waves or caustics. A
        /// drawable is water when its own mask carries no bit outside this one, because a node
        /// mask defaults to all ones. The harness places an analytic sea of its own (`addWater`).
        void setWaterMask(osg::Node::NodeMask mask) { mWaterMask = mask; }

        /// Names the node mask the game puts on the root of a class of thing — an actor, an effect,
        /// the player's own arms in first person — by the same rule as the water's: a node whose
        /// mask carries no bit outside this one. Everything under such a node is placed as that
        /// class, for a camera's cull mask to keep or leave out (`InstanceClass`). The harness names
        /// nothing here, so everything it walks is `Static`.
        void setClassMask(InstanceClass what, osg::Node::NodeMask mask);

        /// How far under a walk's root a `SceneUtil::StableIdentity` is looked for: nought is the
        /// root alone, which is what a caller that stamps nothing wants. The game stamps the
        /// scene root's children and grandchildren — a cell root, and a reference root under it —
        /// and says two; a look deeper than the stamps go is a line of every node loaded for
        /// nothing.
        void setStampDepth(unsigned int depth) { mStampDepth = depth; }

        /// Where the walks that follow are looked at from, for a billboard to face: the camera's
        /// own basis, `viewBasisOf` its inverse view. Nothing, which is what a fresh extractor
        /// holds, leaves a billboard at its base rotation.
        ///
        /// **Told per walk and read per billboard, because a `NifOsg::AutoTransform` turns only
        /// under a cull visitor** — `computeMatrix` asks the visitor for a `CullStack` and takes
        /// its last rotation otherwise — and no walk in this renderer is one. What a cull hands it
        /// is the eye, the look and the up in the node's own frame, and that is what the walk
        /// hands `computeMatrixForFrame` instead.
        void setEye(const std::optional<ViewBasis>& eye) { mEye = eye; }

        /// The nodes the game put somewhere else in one step since the last walk — a door, a
        /// teleport — whose placements the walks that follow stand with no motion
        /// (`PlacementTable::jump`), so a history from where they stood is refused where they
        /// stand. Told per walk, as the eye is, and kept alive by the caller over the walks it is
        /// told for.
        void setJumped(std::span<const osg::Node* const> jumped) { mJumped = jumped; }

        /// The world's clock, in seconds, once per frame: what everything the graph animates is
        /// driven by. `SceneUtil::FrameTimeSource` reads the simulation time off the visitor's frame
        /// stamp, so a mirror with a clock of its own would run the game's fires while the game is
        /// paused.
        ///
        /// **And the emitters' clock, moved on by `step`**: the world's step this frame stands for,
        /// however a script scales it, and nought where the game stood paused, as the rasterizer's
        /// cull hands `osgParticle` the world's step. A step and not the gap since the last call,
        /// because the clock may be set as well as stepped — a harness standing a world at a
        /// moment — and a gap of an hour is three hundred and sixty thousand particles of a plume
        /// in one frame. The first call only starts it, and a step backward is none. Also the
        /// sequence every emitter's once-per-frame guard is kept against, so however many walks
        /// reach one, exactly one of them steps it.
        void setSimulationTime(double seconds, double step);

        /// Walks `node` and places what it finds by `transform`, under `anchor`. A subtree, and it
        /// never reaches the ring: the precipitation node would otherwise place the ground a
        /// second time. `extractWorld` is the call that means the whole of it.
        ///
        /// @param anchor what the caller is placing, stable for as long as it stands. A node path
        ///        does not identify a placement on its own: OpenMW hands out one template node per
        ///        model, and a hundred crates are a hundred calls on that node differing only in
        ///        `transform`. A caller that walks one whole graph can pass zero.
        /// @param frame the game's own, which is what tells a semi-active `SceneUtil::Skeleton` it
        ///        was reached. A caller with no actors in its graph can leave it.
        ExtractionStats extract(
            const osg::Node& node, const osg::Matrixf& transform, std::size_t anchor, std::size_t frame = 0);

        /// The same, for the walk that is the whole world — the one `retire` is sound after — with
        /// what the graph does not parent walked inside it: the cell ring's ground, statics and
        /// lamps, which have no node anywhere. A reference and not a stored pointer, so no caller
        /// can be the one that forgets it and has the distant ground swept on every frame after the
        /// first, and the extractor holds nothing of the ring between two walks.
        ///
        /// **`ring` is collected for `frame`**, the walk's own: a ring told one frame and walked for
        /// another adopts twice on a frame walked twice.
        ///
        /// **At the world's own frame and no transform above it**, because what it freezes is
        /// keyed on each run's place under the root (`FrozenFace`), and a root that moved would
        /// leave every frozen run where it stood.
        ///
        /// @param ring one made on this extractor, which is what it adopts through; asserted.
        ExtractionStats extractWorld(const osg::Node& root, std::size_t anchor, std::size_t frame, CellRing& ring);

        /// `extract`, for what the weather drops: every emitter met under `fall` is placed as one
        /// whose sprites a roof keeps off — `MirrorPass::mFalls`. The precipitation's walk and
        /// nothing else, because a hearth's smoke under a roof is where it belongs.
        ///
        /// **Stood at `eye`**: the drops hang under the sky's camera-relative transform, so their
        /// particles are placed about the origin, and the eye they were driven with is the one place
        /// the box travels nowhere — a sprite's travel between two frames is then its fall.
        ///
        /// @param fall the sky manager's rain box or its driven effect, or null for a world with
        ///        nothing of that kind over it or a fall the game hides — under water it freezes the
        ///        drops where they stand, and walked anyway they hang in the air.
        /// @param anchor as `extract` takes it: the rain and the driven effect are two roots the walk
        ///        cannot tell apart by structure.
        ExtractionStats extractPrecipitation(
            const osg::Node* fall, const osg::Vec3f& eye, std::size_t anchor, std::size_t frame = 0);

        /// What this thread's walks share. Its preprocessor's counts are the thread's and not a
        /// walk's, so whoever owns the frame takes them once, after the frame's last walk.
        WalkContext& getContext() { return mContext; }

        SpecularLayout getSpecularLayout() const override { return mContext.mSpecular; }

        /// Lets go of everything the walks stood and the ring held, for a world that is detached:
        /// the ring's holds are given back — the two releases read nothing of a walk — and then a
        /// retire at a fresh epoch, which keeps nothing but what is held, because a walk that will
        /// not happen has met nothing. What is left of the world in the scene is nothing, which
        /// `SceneDesc::isEmpty` says. Between walks, and the ring already told of no world.
        Retirement detach(CellRing& ring);

        /// Drops everything the walks since the last call did not find — placements included — and
        /// with it the holds those entries took on the scene's rows, which a row goes with the last
        /// of. Only sound where the walks were the whole of what the scene is of, which every
        /// caller's are: the world's frame and a picture's subject are each re-walked whole. Also
        /// the only thing that lets go of the identity maps, which own their keys, so a caller that
        /// never retires holds every drawable it has ever walked.
        ///
        /// **Between the last walk and the hand-over, which the scene enforces.** Handed over
        /// before this, the scene still holds what the walk stopped finding, where the last frame
        /// left it: `SceneDesc::noteWalked` marks the scene at every walk and this is what clears
        /// the mark, and a hand-over of a marked scene is a call out of its turn.
        Retirement retire();

        /// What the sweeps and the thaws let go of since the owner last handed it over: the roots
        /// a walk stopped meeting, and the game's objects the maps held. **The owner owes the
        /// hand-over**, to whatever releases the game's own, and clears it after; until then it
        /// keeps a cell the world unloaded alive.
        Released& getReleased() { return mReleased; }

    private:
        /// Where the extractor stands: between walks, or inside one. A walk inside a walk would
        /// point the pass at a second set of counts and lose the first's, and a retire inside one
        /// would sweep what the walk is about to stamp; both are asserted where they happen.
        enum class Phase
        {
            Between,
            Walking,
        };

        /// The pass opened for one walk and closed however the walk ends: the counts pointer back
        /// to null, the falls flag cleared, no reference's record left open and the phase back to
        /// `Between`, on the ordinary return and on a throw alike. A resolver reached after a walk
        /// that threw would otherwise count into an unwound local, and the next walk's first
        /// reference would be recorded inside the one the throw left.
        class WalkGuard
        {
        public:
            WalkGuard(MirrorPass& pass, Stepped<Phase>& phase, ExtractionStats& stats, bool falls, bool& recording);
            ~WalkGuard();

            WalkGuard(const WalkGuard&) = delete;
            WalkGuard& operator=(const WalkGuard&) = delete;

        private:
            MirrorPass& mPass;
            Stepped<Phase>& mPhase;
            bool& mRecording;
        };

        /// What the ring may do, and nothing else may. `Rtx::SceneAdopter` is implemented
        /// privately, so its calls are reachable through that interface and not in front of every
        /// reader of this class. The two adoptions mean anything only inside a walk; the
        /// two releases are allowed between walks as well — `detach` — because giving a hold
        /// back reads nothing of a walk.
        Index adoptMesh(const osg::Drawable& drawable, const MeshReading& reading) override
        {
            mPhase.expect(Phase::Walking);
            return mMeshes.adopt(drawable, reading);
        }
        MaterialResolver::Resolved adoptMaterial(
            const MaterialReading& reading, std::span<const osg::StateSet* const> chain) override;
        void releaseMesh(const osg::Drawable& drawable) override { mMeshes.release(drawable); }
        void releaseMaterial(const osg::StateSet* key) override { mMaterials.release(key); }
        SceneDesc& getScene() override { return mScene; }
        ExtractionStats& getStats() override { return mPass.getStats(); }

        /// Walks the graph and hands every geometry it meets to the extractor, through the calls
        /// below, which nothing else may make.
        class Traversal;

        /// Places one light. The graph and not the content files, because that is where a light
        /// that moves with the thing carrying it exists: a torch in an NPC's hand is no cell
        /// record, and neither is a lamp something picked up and put down.
        ///
        /// @param glow the effect the light hangs on, where the walk is inside one.
        /// @param owner the class of the placement the light hangs under, which a view hides it with.
        void addLight(const SceneUtil::LightSource& source, const osg::Matrixf& place, double simulationTime,
            std::optional<std::size_t> glow, InstanceClass owner);

        /// Opens the glow of a magic effect the walk has entered — `Rtx::Glow` — and returns its
        /// index, which the walk carries down the effect's subtree. Not yet a lamp, because the
        /// effect's flames are read after the walk, with the other emitters; `walk` makes the
        /// lamps once they are. `owner` is the class the effect stands under, which its lamp answers
        /// to.
        std::size_t openGlow(InstanceClass owner);

        /// Resolves one drawable and places it. `place` is handed over rather than worked out from
        /// `path`, because `osg::computeLocalToWorld` rebuilds the whole chain from the root for
        /// every drawable. A drawable and not an `osg::Geometry`, because a skinned body is an
        /// `osg::Drawable` over a source geometry — the bind pose — beside the rig that poses it.
        ///
        /// @param glow the effect the drawable stands under, where the walk is inside one.
        /// @param jumped whether the drawable stands under a node `setJumped` named.
        /// @param lampBody whether the drawable is the model of a light that gives light
        ///        (`MeshInstance::mLampBody`).
        void addDrawable(const osg::Drawable& drawable, std::size_t who, std::span<const Shading> shading,
            const osg::Matrixf& place, InstanceClass what, std::optional<std::size_t> glow, bool jumped, bool lampBody);

        /// The state set a node shades with where that is not the one it wears, or null where it
        /// is — `MaterialResolver::animate`, which says what the two cases are. Applied here rather
        /// than left to a callback: a `SceneUtil::StateSetUpdater` as a cull callback writes a state
        /// set that exists only inside a cull traversal, and as an update callback alternates the
        /// node's own between two copies, so a material keyed on the address is added and swept
        /// once a frame. One state set per placement of the node, rewritten in place, keeps the
        /// address stable.
        ///
        /// @param placement the identity of the path the walk reached `node` by.
        /// @param underAnimated whether an animated state set stands above `node` on the chain,
        ///        which makes a node with a state set of its own animated too.
        const osg::StateSet* animate(osg::Node& node, std::size_t placement, bool underAnimated);

        /// Whether a drawable carrying `mask` is the world's water.
        bool isWater(osg::Node::NodeMask mask) const;

        /// The class a node carrying `mask` states, or nothing where it states none. Asked by the
        /// walk at every node, which is why it is not the drawable's own question like water.
        std::optional<InstanceClass> classOf(osg::Node::NodeMask mask) const;

        /// Whether `mask` carries no bit outside `named`, which is what both questions above ask.
        static bool carriesOnly(osg::Node::NodeMask mask, osg::Node::NodeMask named);

        /// What the three walks are, differing only in whether the ring's geometry is asked for
        /// and whether what is placed falls.
        ExtractionStats walk(const osg::Node& node, const osg::Matrixf& transform, std::size_t anchor,
            std::size_t frame, CellRing* ring, bool falls);

        /// One placement of a frozen subtree, by what the walk resolved it under: the placement's
        /// identity, the drawable its mesh is keyed on and its material's key, each held until the
        /// subtree thaws. A drawable the mesh resolver refused stands nothing, and only its refusal
        /// is held.
        struct FrozenKey
        {
            std::size_t mPlacement = 0;
            const osg::Drawable* mDrawable = nullptr;
            const osg::StateSet* mMaterial = nullptr;
            bool mPlaced = false;
        };

        /// What can change a frozen subtree from outside it, as its root stands: where the root is
        /// in the world, its state set, and its children, by their count and the first of them.
        struct FrozenFace
        {
            osg::Matrix mWorld;
            const osg::StateSet* mStateSet = nullptr;
            const osg::Node* mFirstChild = nullptr;
            unsigned int mChildren = 0;

            static FrozenFace of(const osg::Node& root, const osg::Matrix& world);

            bool operator==(const FrozenFace& other) const = default;
        };

        /// A subtree under a reference root whose walk met nothing that changes between frames
        /// on its own — `Traversal::enter` says what does — and which the world walk passes rather
        /// than descends while its `FrozenFace` stands. Its entries are held, so every sweep it is
        /// not walked in keeps them, and nothing in it is read again until it thaws.
        ///
        /// **A root whose face changed stays thawed until a walk after that finds it standing
        /// still**, its run kept with no keys and the face it was last met at. A door the game turns
        /// a step a frame, or a reference a script moves, would otherwise thaw, freeze again where
        /// it stood and thaw on the next frame: a hold taken and given back on every entry, and a
        /// run allocated and freed, on every frame of the motion.
        struct FrozenRun
        {
            FrozenFace mFace;
            Run mKeys;

            /// What its walk counted, which every walk that passes it counts again.
            std::uint32_t mInstances = 0;

            /// The world walk that last met it, and the one that last found its face changed.
            unsigned int mMet = 0;
            unsigned int mMoved = 0;

            /// Whether its keys are held and the walk passes it: false while it stands thawed.
            bool mHolding = false;
        };

        /// Whether the reference root `root`, standing at `world`, is frozen and still what it
        /// froze as: counted as walked and passed where it is, and thawed where it is not, for the
        /// walk to descend into it as any other. A thawed root is walked, and its face kept.
        bool passFrozen(const osg::Node& root, const osg::Matrix& world);

        /// Starts recording what the world walk resolves under the reference root it is about to
        /// descend into, and `endFrozen` freezes the root with it where `changeable` is false,
        /// nothing it resolved said otherwise, and its face did not change on this walk.
        void recordFrozen();
        void endFrozen(const osg::Node& root, const osg::Matrix& world, bool changeable);

        /// Gives a frozen subtree's holds back, as the walk that thaws it or a sweep that missed it.
        void thaw(FrozenRun& run);

        /// Thaws every frozen subtree the world walk did not meet — gone from the graph, or masked
        /// out — ahead of the sweep, which would otherwise keep its rows for ever.
        void thawUnmet();
        void thawAll();

        SceneDesc& mScene;

        /// What kind each class this walk meets is, node or drawable. A member because the
        /// answers are a fact about the classes in the world rather than about one frame, and
        /// before the walk, which reads it.
        NodeKinds mKinds;

        /// The walk itself, made once rather than per call, because the chain of state sets it
        /// refills as it descends would be a per-frame allocation as a local.
        std::unique_ptr<Traversal> mWalk;

        /// What this thread's walks share, the owner's.
        WalkContext& mContext;

        /// Every node until the owner states a mask (`setTraversalMask`).
        osg::Node::NodeMask mTraversalMask;

        /// See `setStampDepth`.
        unsigned int mStampDepth = 0;

        /// Which drawables are the sea. Zero means none of them, which is every caller that has not
        /// said otherwise.
        osg::Node::NodeMask mWaterMask = 0;

        /// The root mask of each class but `Static`, which is what a node states none of. Zero
        /// means the class is never stated, which is every caller that has not said otherwise.
        struct ClassMask
        {
            InstanceClass mClass;
            osg::Node::NodeMask mMask = 0;
        };
        std::array<ClassMask, 3> mClassMasks{ ClassMask{ InstanceClass::Actor }, ClassMask{ InstanceClass::Effect },
            ClassMask{ InstanceClass::FirstPerson } };

        /// See `setEye`.
        std::optional<ViewBasis> mEye;

        /// See `setJumped`.
        std::span<const osg::Node* const> mJumped;

        /// Every effect this walk entered, in the order it entered them. Which of them the walk
        /// is inside is the traversal's, carried down the subtree beside the class. Reserved once,
        /// `sEffectBudget`.
        std::vector<Glow> mGlows;

        /// Which sweep is current, and where the walk in progress puts its counts. Declared before
        /// the walk and every resolver below, which borrow it rather than keep a copy that could
        /// fall behind.
        MirrorPass mPass;

        Stepped<Phase> mPhase{ Phase::Between };

        /// Which slot each placement holds, and when it was last met. One lookup a placement a
        /// frame, and the scene keeps the transform.
        Kept<boost::unordered_flat_map<std::size_t, Known>> mPlacements{ mPass };

        /// The drawables the walk met, and what poses the ones that deform.
        MeshResolver mMeshes{ mScene, mPass, mContext.mContent.mPreprocessor };

        /// What the content says each surface is, and the textures those name.
        MaterialResolver mMaterials{ mScene, mPass, mContext.mContent, mContext.mSpecular };

        /// The keys the walk's chains of state sets fold to, which `mMaterials` holds its entries
        /// under, and the ring's readings as they are adopted.
        ChainKeys mChainKeys;

        /// What a groundcover reading's key is paired with last (`MaterialReading::mGroundcover`):
        /// an empty state set of the walk's own, standing for the override no chain states.
        const osg::ref_ptr<const osg::StateSet> mGroundcoverOverride = new osg::StateSet;

        /// The particle systems the walk met, and the sprite textures they hold.
        EmitterResolver mEmitters{ mScene, mPass, mContext.mContent };

        /// The frozen subtrees, by their roots, held so a root the game freed cannot be mistaken
        /// for the one built where it stood; their keys in one buffer, a run a subtree; and what the
        /// root being walked has resolved so far. Reserved and kept, never freed.
        boost::unordered_flat_map<osg::ref_ptr<const osg::Node>, FrozenRun, ByAddress<const osg::Node>,
            ByAddress<const osg::Node>>
            mFrozen;
        RunBuffer<FrozenKey> mFrozenKeys;

        /// Kept and refilled: grown to the busiest crossing so far, and never shrunk.
        Released mReleased;
        std::vector<FrozenKey> mRecorded;

        /// Whether a root is being recorded, whether what it resolved can change on its own — the
        /// sea's material, a particle system — and the instance count its walk began at.
        bool mRecording = false;
        bool mRecordedChangeable = false;
        std::uint32_t mRecordedFrom = 0;

        /// The number of the world walk in progress or last made, which a frozen run it met
        /// carries, and the traversal mask the frozen subtrees were walked under: a mask that
        /// changes is a view that sees another part of every subtree.
        unsigned int mWorldWalk = 0;
        osg::Node::NodeMask mFrozenMask = 0;

        /// How many meshes and materials the scene had freed at the last retire, which the next
        /// one counts what went from.
        std::uint64_t mMeshesFreed = mScene.meshes().getFreedCount();
        std::uint64_t mMaterialsFreed = mScene.materials().getFreedCount();
    };
}
