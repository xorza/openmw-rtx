#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <osg/Matrixf>
#include <osg/Node>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/scenedesc.hpp>

namespace osg
{
    class Drawable;
    class Geometry;
    class StateSet;
}

namespace osgParticle
{
    class ParticleSystem;
}

namespace SceneUtil
{
    class LightSource;
}

namespace Terrain
{
    class TerrainDrawable;
}

namespace RtxBridge
{
    /// What one extraction pass did.
    ///
    /// The reused counts are the interesting half: a mirror that adds nothing on a second pass over
    /// an unchanged graph is the property the whole incremental design rests on, and it is only
    /// visible as a number.
    struct ExtractionStats
    {
        /// Distinct geometry met for the first time, so one new entry in the scene each.
        std::uint32_t mMeshesAdded = 0;
        std::uint32_t mMaterialsAdded = 0;

        /// Drawables that resolved to something already known. A count of lookups, not of meshes:
        /// a hundred crates sharing one model contribute a hundred here and one above.
        std::uint32_t mMeshesReused = 0;
        std::uint32_t mMaterialsReused = 0;
        std::uint32_t mInstances = 0;

        /// Drawables whose vertices are recomputed every frame and so were read from the pose
        /// rather than from the cache: skinned bodies and morphed faces. Each one already met is a
        /// bottom-level structure a backend has to build again, which is what makes this the cost
        /// of an actor rather than a count of them.
        std::uint32_t mDeformed = 0;

        /// Particle systems met, and the live particles they were holding.
        ///
        /// **Sprites and not triangles**, so neither number is a mesh or an instance: an emitter is
        /// a sphere and a run of discs the primary ray composites, and nothing about it reaches an
        /// acceleration structure. An emitter whose particles have all died places nothing and is
        /// not counted.
        std::uint32_t mEmitters = 0;
        std::uint32_t mSprites = 0;

        /// Drawables this cannot read at all — neither an `osg::Geometry`, nor either of the two
        /// deforming kinds, nor a particle system.
        ///
        /// What is left is OpenMW's own debug drawing, which a ray tracer answers differently
        /// rather than misses. A canary and not a deficit: what it would catch is a new kind of
        /// drawable arriving unnoticed.
        std::uint32_t mSkippedUnknown = 0;

        /// What the textures a scene reached for turned out to be.
        ///
        /// Kept because the answer decides how they are uploaded, and guessing it from what the
        /// content files ought to contain is how a renderer ends up with a path nothing takes.
        std::map<std::string, std::uint32_t> mTextureFormats;

        /// Geometry with no vertices or no triangles. Morrowind ships some.
        std::uint32_t mSkippedEmpty = 0;

        /// Lamps taken off the graph. Zero in the harness, which has no `LightManager` and reads
        /// the cell's `LIGH` records instead.
        std::uint32_t mLights = 0;

        ExtractionStats& operator+=(const ExtractionStats& other);
    };

    /// What one sweep dropped.
    struct Retirement
    {
        std::uint32_t mMeshes = 0;
        std::uint32_t mMaterials = 0;
        std::uint32_t mTextures = 0;

        bool empty() const { return mMeshes == 0 && mMaterials == 0 && mTextures == 0; }
    };

    /// Mirrors an OpenSceneGraph subtree into a `Rtx::SceneDesc`.
    ///
    /// The identity maps live across calls, so the same geometry met again — in another cell, under
    /// another reference, in a later frame — resolves to the mesh already uploaded rather than to a
    /// copy of it. That is what makes an incremental mirror possible instead of a rebuild per frame,
    /// and it is why this is an object rather than a function.
    class SceneExtractor
    {
    public:
        explicit SceneExtractor(Rtx::SceneDesc& scene)
            : mScene(scene)
        {
        }

        SceneExtractor(const SceneExtractor&) = delete;
        SceneExtractor& operator=(const SceneExtractor&) = delete;

        /// Which nodes the walks may descend into, as an `osg` traversal mask.
        ///
        /// **What keeps the mirror out of subtrees the ray tracer answers for itself.** The engine
        /// already marks them — OpenMW's sky is `Mask_Sky` — and a mask is how OSG is asked to skip
        /// one, so nothing here has to know what a sky is. Everything by default.
        void setTraversalMask(osg::Node::NodeMask mask) { mTraversalMask = mask; }

        /// Walks `node` and places what it finds by `transform`, under `anchor`.
        ///
        /// Takes a const reference because nothing here writes to the graph; OSG's visitor API is
        /// non-const throughout regardless, so the cast happens once, here.
        ///
        /// @param anchor what the caller is placing, stable for as long as it stands. **A node path
        ///        does not identify a placement on its own**, because the same subtree is walked
        ///        under many of them: OpenMW hands out one template node per model and a hundred
        ///        crates are a hundred calls on that same node, all with the same path and all
        ///        differing only in the `transform` given here. Anything the caller can keep is a
        ///        good anchor — a reference id, an actor's address, a terrain chunk — and a caller
        ///        that walks one whole graph, where every path is already distinct, can pass zero.
        /// @param frame which of a `SceneUtil::LightSource`'s two buffers to read. The game passes
        ///        the viewer's frame number, which is the one update has just finished writing;
        ///        anything with no `LightManager` in its graph can leave it.
        ExtractionStats extract(
            const osg::Node& node, const osg::Matrixf& transform, std::size_t anchor, std::size_t frame = 0);

        /// Ends a frame: what was placed becomes what was placed before.
        ///
        /// **Called once per frame by whoever is mirroring a live graph, and never by anything that
        /// walks a world once.** Until it is called, every placement's previous transform is its
        /// current one, which is exactly right for a scene that does not move — and a harness that
        /// loads a region and flies a camera round it wants that answer, not a stale one.
        ///
        /// Freeing the slots of placements that have gone is `retire`'s job and not this one, for
        /// the reason written there: only a caller whose walks were the whole world can tell a
        /// placement that has left the graph from one it simply did not visit.
        void advance();

        /// Drops everything the walks since the last call did not find — placements included — and
        /// compacts the scene.
        ///
        /// **Only where the walks were the whole world.** This is mark and sweep: what makes it
        /// sound is that anything alive was met, so a caller that walks a region once and then
        /// mirrors only the movers would retire the region it is standing in. The game re-walks its
        /// whole graph every frame and can call this; the harness keeps a snapshot and does not.
        ///
        /// **It is a soundness fix and not only a memory one.** The identity maps are keyed on raw
        /// `osg` pointers, which is what makes a crate met again resolve to the crate already
        /// uploaded — and an address the engine has freed can be handed back for something else.
        /// Without this the next thing allocated there inherits a mesh it has nothing to do with.
        ///
        /// Anything a caller kept across this — a snapshot of placements, an index of its own — is
        /// stale afterwards, and `Rtx::SceneDesc::getRevision` is what says so.
        Retirement retire();

        /// Places one light. **The graph and not the content files**, because that is where a light
        /// that moves with the thing carrying it exists: a torch in an NPC's hand is no cell
        /// record, and neither is a lamp something picked up and put down.
        void addLight(const SceneUtil::LightSource& source, const osg::NodePath& path, const osg::Matrixf& place,
            std::size_t frame, ExtractionStats& stats);

        /// Resolves one drawable and places it. The visitor's whole contract with this class.
        ///
        /// `place` is where the drawable stands in the world, which the visitor has accumulated on
        /// its way down; `path` is what identifies the placement and where the state that shades it
        /// comes from. **The transform is handed over rather than worked out from the path**,
        /// because `osg::computeLocalToWorld` rebuilds the whole chain from the root for every
        /// drawable and the visitor already holds the part they share.
        ///
        /// **A drawable and not an `osg::Geometry`**, because a skinned body is neither: it is an
        /// `osg::Drawable` holding two internal geometries and writing the pose the cull traversal
        /// just computed into whichever of them was not last drawn. Which of the kinds this is
        /// belongs here rather than to a caller — the visitor would only be asking the same
        /// question with less to answer it from.
        void addDrawable(const osg::Drawable& drawable, const osg::NodePath& path, const osg::Matrixf& place,
            ExtractionStats& stats);

    private:
        /// Places one particle system's live particles as a run of camera-facing discs.
        ///
        /// **The engine's own simulation, read where it stands.** OpenMW runs `osgParticle` under
        /// the update traversal — emitters, colliders, gravity, colour and size ramps, the lot —
        /// so by the time the mirror walks the graph a flame is a list of positions, sizes and
        /// colours. Re-deriving that from the `NiParticleSystemController` would be a second
        /// implementation of the same content, free to disagree with the one the game is running.
        void addEmitter(const osgParticle::ParticleSystem& particles, const osg::NodePath& path,
            const osg::Matrixf& place, ExtractionStats& stats);

        /// What identifies one placement from one frame to the next.
        ///
        /// **The anchor and the node path under it, hashed together.** Neither is enough alone: a
        /// drawable is not an instance, because a hundred crates share one geometry, and a path is
        /// not one either, because a hundred crates walked from a shared template node share the
        /// path as well. What tells them apart is what the caller was placing.
        ///
        /// Hashed rather than kept, because a path is a vector of pointers per placement and the
        /// map is walked every frame; at sixty-four bits over tens of thousands of placements a
        /// collision is not a thing that happens.
        static std::size_t identify(std::size_t anchor, const osg::NodePath& path);

        /// The mesh index for one drawable, adding it or re-reading it as its kind requires.
        ///
        /// **Keyed on the drawable and not on the geometry**, because a deforming drawable's
        /// geometry pointer alternates between its two buffers: keying on that would put two frozen
        /// poses of every actor in the scene and flicker between them.
        Rtx::Index resolveMesh(
            const osg::Drawable& drawable, const osg::Geometry& geometry, bool deforming, ExtractionStats& stats);
        Rtx::Index resolveMaterial(const osg::NodePath& path, ExtractionStats& stats);

        /// The layered material of a terrain chunk, whose shading is not on the graph at all.
        ///
        /// `Terrain::TerrainDrawable` carries one pass per ground texture, each alpha-blended over
        /// the last across the same triangles, because that is how a rasterizer draws a blend it
        /// cannot sample in one go. A ray tracer hits the ground once and shades it once, so the
        /// passes are read back into layers and summed there instead.
        Rtx::Index resolveTerrainMaterial(const Terrain::TerrainDrawable& terrain, ExtractionStats& stats);

        Rtx::SceneDesc& mScene;

        /// An entry in one of the identity maps, and when it was last met.
        ///
        /// The epoch is what `retire` sweeps on: a walk stamps everything it resolves, so anything
        /// still carrying an older stamp is something the graph no longer has.
        struct Known
        {
            Rtx::Index mIndex = Rtx::sNoIndex;
            std::uint64_t mEpoch = 0;
        };

        /// Which slot each placement holds, and when it was last met.
        ///
        /// **This is what a slot buys.** The two maps of matrices it replaces were rebuilt every
        /// frame — a lookup, an insert and a heap node for each of fifty thousand placements, to
        /// carry a transform from one frame to the next that the scene can simply keep. What
        /// remains is one lookup, and Phase 2 is about not making that either.
        std::unordered_map<std::size_t, Known> mPlacements;

        // Keyed on pointer identity, which OpenMW's resource cache and its optimizer's
        // SHARE_DUPLICATE_STATE pass together make meaningful: the same model loaded twice is the
        // same object, and equivalent state sets are collapsed into one.
        std::unordered_map<const osg::Drawable*, Known> mMeshes;
        std::unordered_map<const osg::StateSet*, Known> mMaterials;

        /// Which texture each particle system draws with.
        ///
        /// **Cached for its liveness rather than for its speed**, though it saves a path hash per
        /// emitter per frame as well: a sprite's texture hangs off no material, so this map is the
        /// only thing that can speak for it when the scene is swept.
        std::unordered_map<const osg::Drawable*, Known> mEmitterTextures;

        osg::Node::NodeMask mTraversalMask = ~0u;

        /// What the walk in progress was told it is placing. See `extract`.
        std::size_t mAnchor = 0;

        /// Which sweep is current. Everything a walk resolves or places is stamped with it.
        std::uint64_t mEpoch = 0;

        // Refilled per sweep: the survivors, as the scene wants them.
        std::vector<Rtx::Index> mLiveMeshes;
        std::vector<Rtx::Index> mLiveMaterials;
        std::vector<Rtx::Index> mLiveTextures;
        Rtx::Remap mRemap;

        // Refilled per drawable rather than reallocated, because a cell is tens of thousands of them.
        std::vector<std::uint32_t> mIndexScratch;
        std::vector<osg::Vec3f> mNormalScratch;
        std::vector<osg::Vec2f> mTexCoordScratch;
        std::vector<float> mMaskScratch;

        /// One emitter's sprites, refilled per particle system: the count is only known once the
        /// dead ones have been passed over, and a cell holds tens of these every frame.
        std::vector<Rtx::Sprite> mSpriteScratch;
    };
}
