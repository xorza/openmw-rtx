#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include <osg/BoundingBox>
#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    /// An index into one of `SceneDesc`'s tables, or `sNoIndex` for "none".
    using Index = std::uint32_t;

    inline constexpr Index sNoIndex = ~Index{ 0 };

    /// Where one mesh's vertices and indices sit in the scene's shared buffers.
    ///
    /// The buffers are shared rather than per-mesh because a cell holds thousands of meshes and a
    /// `vector` of `vector`s would pay an allocation for each one — and because the GPU wants one
    /// buffer anyway, so a per-mesh vector would only have to be flattened again on the way up.
    struct MeshRange
    {
        Index mVertexOffset = 0;
        Index mVertexCount = 0;
        Index mIndexOffset = 0;
        Index mIndexCount = 0;

        /// How much room the range was given, which is what a mesh taking this slot over has to fit
        /// inside.
        ///
        /// **A slot keeps its range for as long as the scene lives.** Reclaiming by moving the
        /// survivors down would renumber every mesh, and everything built from a mesh index — every
        /// bottom-level acceleration structure in the world — would have to be built again. So a
        /// freed slot keeps its room and waits for something that fits (`docs/rtx/plan.md` §10).
        Index mVertexCapacity = 0;
        Index mIndexCapacity = 0;

        Index getTriangleCount() const { return mIndexCount / 3; }
    };

    /// How the alpha channel of a surface's diffuse texture is meant to be read.
    enum class AlphaMode
    {
        /// Ignore it. The overwhelming majority of Morrowind's geometry.
        Opaque,

        /// Test against `Material::mAlphaRef`. The case opacity micromaps exist for, and the case
        /// that costs a candidate loop until they are built.
        Cutout,

        /// Blend, and read the opposite of the obvious way: **this is where the foliage is.** Barely
        /// any of Morrowind's material set is alpha-tested outright — a canopy, a grate or a banner
        /// is an `NiAlphaProperty` over a texture whose alpha is all but binary, and the original
        /// renderer sorted it rather than testing it. Marking only the tested ones would look
        /// correct and leave every tree a solid card.
        Blend,
    };

    /// What shading a hit takes, which is not a variation on one path but three different ones.
    enum class MaterialKind
    {
        /// One diffuse texture over a lit surface, which is nearly everything in the game.
        Surface,

        /// A stack of tiling ground textures, each masked by its own grid of weights.
        Terrain,

        /// Water, which has no albedo at all: it reflects, refracts and absorbs, and its colour is
        /// what is behind and above it rather than anything of its own.
        Water,
    };

    /// How a surface is shaded, as recovered from the model.
    ///
    /// Vanilla textures are pre-lit, so `mDiffuse` is not an albedo yet; recovering one is M9. What
    /// is here is what the file says.
    struct Material
    {
        MaterialKind mKind = MaterialKind::Surface;

        Index mDiffuse = sNoIndex;
        Index mNormal = sNoIndex;
        Index mEmissive = sNoIndex;

        osg::Vec4f mDiffuseColour{ 1.0f, 1.0f, 1.0f, 1.0f };

        /// How much the surface glows on its own, with the material's own multiplier folded in.
        ///
        /// The multiplier is not kept apart because nothing wants it apart: the game's own shader
        /// only ever uses their product, and carrying two numbers would be carrying one of them for
        /// the sake of it.
        osg::Vec3f mEmissiveColour{ 0.0f, 0.0f, 0.0f };

        float mAlphaRef = 0.0f;
        AlphaMode mAlphaMode = AlphaMode::Opaque;

        /// Sheet geometry lit and hit from both faces. Morrowind leans on this heavily and a ray
        /// tracer has to be told, because back-face culling is not free the way a rasterizer's is.
        bool mTwoSided = false;

        /// Where this material's terrain layers sit in the scene's layer table.
        ///
        /// Empty for everything that is not terrain, which is all but a handful of materials in a
        /// cell — so the layered path costs the rest of them one comparison and no indirection.
        Index mLayerOffset = 0;
        Index mLayerCount = 0;

        /// The alpha below which a texel is a hole, or zero where the surface has none.
        ///
        /// A blended material that never asked for a test gets a stand-in, because that is where
        /// the game keeps its foliage. Right for a leaf and wrong for a pane of glass, until
        /// ordered transparency gives the second one somewhere else to go.
        float getAlphaCutoff() const;

        /// Whether traversal has to stop and ask this material whether a hit is a hole.
        ///
        /// The one predicate: the build marks an instance non-opaque by this and the shader tests
        /// against the same cutoff, so the two cannot disagree about which triangles reach the
        /// candidate loop. A cutoff with no texture to sample is not one — the mask lives in the
        /// diffuse map's alpha and there is nothing else to read.
        bool isCutout() const { return getAlphaCutoff() > 0.0f && mDiffuse != sNoIndex; }
    };

    /// One layer of a terrain material: a ground texture and the weights that place it.
    ///
    /// Morrowind's ground is a stack of tiling textures, each masked by a small grid of weights that
    /// `ESMTerrain` derives from the land records — and OpenMW draws that stack as one alpha-blended
    /// pass per layer over the same triangles. A ray tracer has one hit and shades it once, so the
    /// stack is read back into layers and summed at the hit instead.
    struct MaterialLayer
    {
        /// The ground texture, which tiles many times across a chunk.
        Index mDiffuse = sNoIndex;

        /// Where this layer's weights begin in the scene's mask table, and the grid they form.
        ///
        /// A zero-sized grid means the layer covers everything: a chunk of a single ground type is
        /// given no mask at all, because there is nothing for it to blend against.
        Index mMaskOffset = 0;
        std::uint16_t mMaskWidth = 0;
        std::uint16_t mMaskHeight = 0;

        /// Chunk texture coordinates to this layer's, as `uv * xy + zw`.
        ///
        /// Read off the texture matrices the terrain builder attached rather than recomputed: the
        /// mask's carries a half-texel inset and a nudge that exist to match the original game, and
        /// deriving them again from the tile size is how the two quietly stop agreeing.
        osg::Vec4f mDiffuseTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
        osg::Vec4f mMaskTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
    };

    /// One point light, placed in the world.
    ///
    /// Everything here is derived rather than read. A `LIGH` record carries a colour and a radius
    /// and **no intensity at all** — the original renderer had a fixed attenuation curve and no
    /// physical units, so brightness fell out of the curve and there is no authored value to be
    /// faithful to.
    struct Light
    {
        osg::Vec3f mPosition;

        /// Radiant intensity, linear, with the colour folded in.
        ///
        /// Scaled by the square of the recorded radius, which is what makes a lantern and a candle
        /// differ by their size rather than by an arbitrary per-light number.
        osg::Vec3f mIntensity;

        /// How far the light reaches, beyond which it contributes exactly nothing.
        ///
        /// **Not the recorded radius.** Morrowind's radii run 64 to 256 units in an interior — a
        /// metre to three and a half — because a fixed falloff curve lit the lamp's own post and an
        /// ambient term filled the room. Here the ambient is real light and the lamps have to be
        /// what lights the place, so the reach is stretched while the brightness is not.
        float mReach = 0.0f;
    };

    /// The sun, as a directional light.
    ///
    /// Far enough away that its rays are parallel, so it has a direction and no position, and its
    /// shadow ray runs to the end of the world rather than to an emitter.
    struct Sun
    {
        /// The unit vector the light travels *along* — from the sun toward the world, which is the
        /// sense OpenMW's own `setSunDirection` is written in. The direction *to* the sun is its
        /// negation.
        osg::Vec3f mDirection{ 0.0f, 0.0f, -1.0f };

        /// Irradiance on a surface square to it, linear. Zero where there is no sun: an interior,
        /// or a night.
        osg::Vec3f mIrradiance;
    };

    /// One mesh placed in the world: a row of the top-level acceleration structure.
    ///
    /// Not `Instance`, which in this namespace is the `VkInstance` a device comes from.
    struct MeshInstance
    {
        /// Object space to world space.
        osg::Matrixf mTransform;

        Index mMesh = sNoIndex;
        Index mMaterial = sNoIndex;

        /// Whether this slot holds anything. A dropped placement leaves its slot behind rather than
        /// closing the gap, because the slot index is what a hit reads back.
        bool isPlaced() const { return mMesh != sNoIndex; }
    };

    /// One live particle, drawn as a disc facing the eye.
    ///
    /// **A particle system carries no triangles at all** — the sprites are the whole of the drawing —
    /// so nothing here reaches an acceleration structure. The layer is marched against the primary
    /// ray and composited instead, which is also what lets it blend in depth order without the
    /// candidate loop an alpha-blended hit would cost traversal.
    struct Sprite
    {
        osg::Vec3f mPosition;

        /// Half the sprite's width in world units, which is what `osgParticle` means by a size: its
        /// quad runs from `-size` to `+size` about the particle and its bounds are expanded by it.
        float mRadius = 0.0f;

        /// Linear, and already carrying wherever the particle's own colour ramp has reached.
        osg::Vec3f mColour{ 1.0f, 1.0f, 1.0f };

        /// What the particle's own fade left of it, multiplied into the texture's alpha at the hit.
        float mAlpha = 1.0f;
    };

    /// One particle system: what its sprites are drawn with, and a sphere that holds all of them.
    ///
    /// **The sphere is the whole spatial structure and it is enough.** A light is asked for by a
    /// shading *point*, which the uniform grid answers in a lookup; an emitter is asked for by a
    /// whole *ray*, which would have to walk that grid cell by cell. There are tens of emitters in a
    /// cell against hundreds of lamps and each is small, so one rejection throws an emitter away for
    /// almost every pixel of the frame.
    struct SpriteEmitter
    {
        osg::Vec3f mCentre;

        /// Far enough from `mCentre` to contain every sprite in the range, rim included.
        float mReach = 0.0f;

        Index mFirst = 0;
        Index mCount = 0;

        /// The sprite texture, or `sNoIndex` where the emitter had none — which draws nothing, since
        /// a particle's whole silhouette is in that texture's alpha.
        Index mTexture = sNoIndex;

        /// `SRC_ALPHA, ONE`: a flame, which adds light and hides nothing behind it. The rest blend
        /// over, which is smoke and needs its colour ramp to fade it.
        bool mAdditive = false;
    };

    /// Everything the renderer needs to know about a world, with no Vulkan and no scene graph in it.
    ///
    /// Lights come from ESM `Light` records rather than from the graph: `NifOsg` never reads
    /// `NiLight`, so a model carries none — a candle's mesh and the light it casts arrive by
    /// different routes and are placed by the same reference.
    ///
    /// Deliberately dumb: it appends and it dedups paths, and nothing else. Deciding that two
    /// drawables are the same mesh belongs to whoever is reading the scene graph, which knows what
    /// identity means there; this type would have to guess.
    class SceneDesc
    {
    public:
        /// Copies the vertex data into the shared buffers and returns the new mesh's index.
        ///
        /// `normals` and `texCoords` may be empty; when they are not they must match `positions` in
        /// length, and `indices` must be a whole number of triangles addressing only those vertices.
        /// All three are contracts on the caller, so they are asserted rather than reported.
        Index addMesh(std::span<const osg::Vec3f> positions, std::span<const osg::Vec3f> normals,
            std::span<const osg::Vec2f> texCoords, std::span<const std::uint32_t> indices);

        /// Replaces one mesh's positions and normals, keeping its topology and its index.
        ///
        /// **What a skinned body or a morphed face is.** Its vertices are recomputed every frame
        /// and its triangles are not, so it keeps the slot in the shared buffers that every
        /// instance already names — a mesh appended afresh each frame would grow the scene without
        /// bound and invalidate every index beside it.
        ///
        /// The counts must match what `addMesh` was given, which deformation never changes;
        /// `normals` may be empty where the mesh has none. Both are contracts on the caller.
        ///
        /// The mesh joins `getDeformed` for the frame, which is what tells a backend whose
        /// acceleration structure to build again.
        void updateMesh(Index mesh, std::span<const osg::Vec3f> positions, std::span<const osg::Vec3f> normals);

        Index addMaterial(const Material& material);

        /// Copies `weights` into the shared mask table and returns where they landed.
        ///
        /// One float per weight rather than the byte the source holds: a mask is a few hundred
        /// texels and a whole cell's worth is tens of kilobytes, which is not worth requiring
        /// 8-bit storage of the device for.
        Index addMask(std::span<const float> weights);

        /// Appends one layer. A material's layers must be added in order and read back as a range,
        /// so there is no index to hand out.
        void addLayer(const MaterialLayer& layer);

        void addLight(const Light& light);

        /// Returns the index of `path`, adding it only if it is not already known.
        Index addTexture(VFS::Path::NormalizedView path);

        /// Places `instance` in a slot and returns it.
        ///
        /// **The slot is the placement's name for as long as it stands.** It is the custom index a
        /// hit reads back, the row a shader looks its material up in, and — because it outlives the
        /// walk that made it — what lets a mirror move a placement instead of rebuilding the list
        /// it was in. A slot freed by `dropInstance` is handed out again; one that is still standing
        /// never is.
        Index addInstance(const MeshInstance& instance);

        /// Moves the placement in `slot`, and says whether that changed anything.
        ///
        /// A transform equal to the one already there is not a move: it writes nothing, records
        /// nothing, and leaves the slot reporting no motion. That is the ordinary case — most of a
        /// world stands still — and making it the cheap one is the point of addressing placements
        /// by slot at all.
        bool moveInstance(Index slot, const osg::Matrixf& transform);

        /// Empties `slot`. Its index is not reused until the next `addInstance` asks for one.
        void dropInstance(Index slot);

        /// Ends a frame's placement: what moved becomes where things were.
        ///
        /// **Costs what moved and not what stands.** Only a slot that reported a move can have a
        /// previous transform that differs from its current one, so only those have to be caught
        /// up — which is what makes a world of fifty thousand placements and three hundred movers
        /// cost three hundred.
        void advancePlacement();

        /// Appends one particle system's live sprites, and the emitter that names them.
        ///
        /// The sphere is derived here rather than passed in, so the rejection test a ray makes and
        /// the sprites it would then walk cannot disagree about where they are. Nothing is added for
        /// an emitter with no live particles, which is most of them for most of a frame.
        void addEmitter(std::span<const Sprite> sprites, Index texture, bool additive);

        /// Drops everything not named and closes the gaps, reporting where the survivors went.
        ///
        /// Frees every mesh, material and texture the caller did not name.
        ///
        /// **The only way a scene loses anything, and nothing is renumbered by it.** A freed entry
        /// keeps its index and its room; the index goes on a free list and the next arrival that
        /// fits takes the slot. Compacting instead — closing the gaps and renaming what pointed into
        /// them — is what made a cell boundary cost a full rebuild: every bottom-level acceleration
        /// structure in the world is named by a mesh index, and every material a hit reads is named
        /// by another. `docs/rtx/plan.md` §10 has the argument.
        ///
        /// A texture survives if it is named here, or if a surviving material or layer still names
        /// it. Layers and masks have no keep set of their own: they belong to the material that owns
        /// them, and a freed material leaks its run until the scene is replaced outright.
        ///
        /// **Placements do not go**, and they no longer have to be carried anywhere either: a slot
        /// is a name, and what it names has stopped moving.
        ///
        /// @param meshes every mesh to keep, each once, in any order.
        /// @param materials the same for materials.
        /// @param textures textures to keep whatever else names them — a particle emitter's sprite
        ///        is on no material, so nothing else would speak for it.
        /// @return whether anything was freed. False is the ordinary frame, and it costs three
        ///         comparisons: a scene that lost nothing has as many survivors as it had entries.
        bool release(std::span<const Index> meshes, std::span<const Index> materials, std::span<const Index> textures);

        /// Empties every table while keeping the capacity, so rebuilding a scene does not go back to
        /// the allocator for buffers it already had.
        void clear();

        /// Empties the per-frame lists a walk rebuilds wholesale: lights, deformed meshes, sprites
        /// and emitters.
        ///
        /// **Placements are not among them.** They are addressed by slot and reconciled in place —
        /// `addInstance` for one that has appeared, `moveInstance` for one that has shifted,
        /// `dropInstance` for one that has gone — because a slot index is what a hit reads back and
        /// because a world of fifty thousand placements of which three hundred move should cost
        /// three hundred. Everything above is small enough per frame that rebuilding it is cheaper
        /// than reconciling it.
        void clearPlacement();

        std::span<const osg::Vec3f> getPositions() const { return mPositions; }
        std::span<const osg::Vec3f> getNormals() const { return mNormals; }
        std::span<const osg::Vec2f> getTexCoords() const { return mTexCoords; }
        std::span<const std::uint32_t> getIndices() const { return mIndices; }

        /// Every mesh slot, live or free. A freed one has a zero count and keeps its room, so a
        /// backend that walks these builds a structure over nothing rather than over somebody else's
        /// triangles — and the top level a frame rebuilds is what stops it being traced.
        std::span<const MeshRange> getMeshes() const { return mMeshes; }

        /// Which meshes changed shape since the last `clearPlacement`, each named once and in no
        /// particular order. Empty for a world that only moves.
        std::span<const Index> getDeformed() const { return mDeformed; }
        /// Every slot, standing or empty, in slot order. `MeshInstance::isPlaced` tells them apart.
        std::span<const MeshInstance> getInstances() const { return mInstances; }

        /// How many slots hold a placement, which is what reaches an acceleration structure.
        std::uint32_t getPlacedCount() const { return mPlacedCount; }

        /// Where each slot stood before the last `advancePlacement`, indexed alongside the slots.
        std::span<const osg::Matrixf> getPrevious() const { return mPrevious; }

        /// The slots that have moved since the last `advancePlacement`, each once.
        ///
        /// A placement that has just been made is here too: a slot nothing has uploaded yet needs
        /// writing for the same reason one that moved does.
        std::span<const Index> getMoved() const { return mMoved; }
        std::span<const Material> getMaterials() const { return mMaterials; }
        std::span<const MaterialLayer> getLayers() const { return mLayers; }
        std::span<const Light> getLights() const { return mLights; }
        /// How many times the scene's **structure** has changed: its meshes and its textures.
        ///
        /// **What a rebuild costs is why this is separate from the tables.** A mesh appearing means
        /// a bottom-level acceleration structure that does not exist yet, and a texture appearing
        /// means an array that has to be made again — hundreds of milliseconds between them, and
        /// the temporal history goes with them. Nothing else in the scene is worth that.
        ///
        /// **The only honest test for it**: comparing table sizes misses a cell that left as another
        /// arrived, which is exactly what walking across a boundary does — and it misses a freed
        /// slot taken over by something else entirely, which is what one does now. Bumped by a mesh
        /// or a texture appearing, whether at the end of the table or into a slot something else
        /// left, and by `clear`; never by a placement, which is rewritten every frame anyway.
        std::uint64_t getStructureRevision() const { return mStructureRevision; }

        /// Forgets which slots have arrived, for a caller that has uploaded them.
        ///
        /// **Whoever hands the scene to a backend owns this**, not the frame: an arrival lives from
        /// the walk that made it until something has taken it, and a walk that is never handed over
        /// must not lose what it added.
        void clearArrivals() { mArrivedTextures.clear(); }

        /// Which texture slots have been written since the last `clearArrivals`.
        ///
        /// **A list and not a count, because a slot is taken over wherever it sits.** A backend used
        /// to be handed the tail of the table and told to append; reclaiming a slot means an arrival
        /// can be anywhere, so the arrivals say where each one goes and the backend writes those and
        /// nothing else.
        std::span<const Index> getArrivedTextures() const { return mArrivedTextures; }

        /// How many times a **mesh** has appeared, which is the expensive half of the above.
        ///
        /// A texture arriving is an upload; a mesh arriving is a bottom-level acceleration structure
        /// that does not exist yet. Told apart because a body texture nobody has worn yet must not
        /// cost the structures of a whole cell.
        std::uint64_t getMeshRevision() const { return mMeshRevision; }

        /// How many times the scene has been **replaced outright** by `clear`.
        ///
        /// **The one thing that still renumbers, and it is travel.** Walking out of the world you
        /// were in — a door, a fast travel, a load — is not a ring arriving, and rebuilding is both
        /// correct and what the player is already waiting for. Everything short of that appends.
        std::uint64_t getResetRevision() const { return mResetRevision; }

        /// How many times the scene's **shading tables** have changed: materials, layers and masks.
        ///
        /// Separate from the structure because the answer is different — a few kilobytes written
        /// where a structure change costs every acceleration structure in the scene. Anything that
        /// animates a state set churns these, and the mirror must not read that as a world arriving.
        std::uint64_t getShadingRevision() const { return mShadingRevision; }

        std::span<const Sprite> getSprites() const { return mSprites; }
        std::span<const SpriteEmitter> getEmitters() const { return mEmitters; }
        std::span<const float> getMasks() const { return mMasks; }
        std::span<const VFS::Path::Normalized> getTextures() const { return mTextures; }

        /// The vertices of one mesh, for a test or a build that wants to read back what it appended.
        std::span<const osg::Vec3f> getMeshPositions(Index mesh) const;
        std::span<const std::uint32_t> getMeshIndices(Index mesh) const;

        std::uint32_t getTriangleCount() const;

        /// The world-space extent of everything placed. Invalid when nothing is.
        ///
        /// Computed from each mesh's local box carried through its instances rather than from every
        /// vertex of every instance, which is the difference between eight transforms per instance
        /// and several hundred.
        osg::BoundingBoxf getBounds() const;

        /// Bytes held by the vertex and index buffers. What the upload at M3 will cost.
        std::size_t getGeometryBytes() const;

    private:
        std::vector<osg::Vec3f> mPositions;
        std::vector<osg::Vec3f> mNormals;
        std::vector<osg::Vec2f> mTexCoords;
        std::vector<std::uint32_t> mIndices;
        std::vector<MeshRange> mMeshes;
        std::vector<Index> mDeformed;

        // Slot-addressed and parallel: the placement, and where it stood before the last advance.
        // Two flat arrays rather than one struct, because the previous transform is read only for
        // what moved and a frame walks the placements for other reasons.
        std::vector<MeshInstance> mInstances;
        std::vector<osg::Matrixf> mPrevious;
        std::vector<Index> mMoved;
        std::vector<Index> mFreeSlots;
        std::uint32_t mPlacedCount = 0;

        std::vector<Material> mMaterials;
        std::vector<MaterialLayer> mLayers;
        std::vector<Light> mLights;
        std::vector<Sprite> mSprites;
        std::vector<SpriteEmitter> mEmitters;
        std::vector<float> mMasks;
        std::vector<VFS::Path::Normalized> mTextures;

        std::uint64_t mStructureRevision = 0;
        std::uint64_t mMeshRevision = 0;
        std::uint64_t mResetRevision = 0;
        std::uint64_t mShadingRevision = 0;

        /// Copies one mesh's arrays into the room `range` names. Zero-fills an attribute the mesh
        /// did not bring, because a reused slot still holds its last tenant's.
        void writeMesh(const MeshRange& range, std::span<const osg::Vec3f> positions,
            std::span<const osg::Vec3f> normals, std::span<const osg::Vec2f> texCoords,
            std::span<const std::uint32_t> indices);

        /// Slots nothing stands in, waiting for something that fits.
        ///
        /// **A list and not a hole map**, because what goes on them is what one departing ring left
        /// — tens of entries, not the table. `mFreeMeshes` is searched for the best fit and the
        /// other two are taken from the back, since a material and a texture are one size.
        std::vector<Index> mFreeMeshes;
        std::vector<Index> mFreeMaterials;
        std::vector<Index> mFreeTextures;

        /// Texture slots written since the last `clearArrivals`, which is what a backend uploads.
        std::vector<Index> mArrivedTextures;

        // The scan this replaces was O(materials x textures). A cell is a hundred of each and would
        // never have noticed; a worldspace is thousands of both, and load time is not the place to
        // find that out. `VFS::Path::Hash` is transparent, so a lookup by view costs no string.
        std::unordered_map<VFS::Path::Normalized, Index, VFS::Path::Hash, std::equal_to<>> mTextureIndex;
    };
}
