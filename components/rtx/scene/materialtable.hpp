#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include <components/misc/digestwords.hpp>
#include <components/rtx/common/rowvector.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/common/slots.hpp>

#include "material.hpp"
#include "sceneroom.hpp"
#include "textureasks.hpp"
#include "texturetable.hpp"

namespace Rtx
{
    /// A mask placed since the last `clearArrivals`: its run, and where its weights start in `ArrivedRuns::mWeights`.
    struct ArrivedMask
    {
        Run mRun;
        std::uint32_t mStart = 0;
    };

    /// The runs a material table has placed since the last `clearArrivals`, and the masks' weights: the host's one
    /// copy of them, kept for one hand-over (`docs/architecture.md`, "The geometry has one host copy, for one
    /// hand-over"). Lists and not a `SlotSet`, because a run is an offset and a count rather than a slot.
    struct ArrivedRuns
    {
        /// What the weights keep of their capacity past `clear`, so a crossing's masks allocate nothing: a world's
        /// first hand-over, its every land cell's, is given back.
        static constexpr std::size_t sKeptBytes = std::size_t{ 1 } << 20;

        std::vector<Run> mLayers;
        std::vector<ArrivedMask> mMasks;
        std::vector<float> mWeights;

        std::span<const float> weightsOf(const ArrivedMask& mask) const
        {
            return std::span<const float>(mWeights).subspan(mask.mStart, mask.mRun.mCount);
        }

        void clear()
        {
            mLayers.clear();
            mMasks.clear();
            if (mWeights.capacity() * sizeof(float) > sKeptBytes)
                mWeights = std::vector<float>();
            else
                mWeights.clear();
        }
    };

    /// Every material the scene holds, the terrain layers they name, and the weights those place.
    /// One type, because a material's textures have to be given back before the run that says
    /// which they were is handed to the next chunk. The textures are the scene's, handed in per
    /// call rather than held: a table that held a reference to the texture table would be left
    /// pointing at the scene a defaulted move came from.
    class MaterialTable : public HeldRows<Material, MaterialIndex>
    {
    public:
        /// Room for `room`'s materials, in the rows and the set of those written, and for its layers
        /// and masks, so no arrival up to it moves a row or a run.
        void reserve(const SceneRoom& room)
        {
            reserveRows(room.mMaterials);
            mAsks.reserve(room.mMaterials);
            mWritten.reserve(room.mMaterials);
            mLayers.reserve(room.mLayers);
        }

        /// Takes a run of the device's mask table for `weights`, files them with the arrivals until the next hand-over
        /// and returns the run. One float per weight rather than the byte the source holds: a cell's worth is tens of
        /// kilobytes.
        Run addMask(std::span<const float> weights);

        /// Copies a material's layers into the shared layer table and returns where they landed —
        /// all of them at once, because a run that can be given back has to be asked for by length.
        Run addLayers(std::span<const MaterialLayer> layers);

        std::span<const MaterialLayer> getLayers() const { return mLayers.getAll(); }

        /// Where the masks' runs are handed out: how far the device's mask table reaches.
        const RunAllocator& getMaskRuns() const { return mMaskRuns; }

        /// Makes a digest of each mask as it arrives, which nothing reads but the harness: asked of a table that holds
        /// nothing yet, so every mask has one.
        void keepDigests();

        /// What the standing mask at `run` digested to as it arrived (`keepDigests`).
        const Misc::DigestWords& getMaskDigest(Run run) const;

        /// Every standing mask's digest, by its run's offset, in no order (`keepDigests`).
        const boost::unordered_flat_map<std::uint32_t, Misc::DigestWords>& getMaskDigests() const
        {
            return mMaskDigests;
        }

        std::span<const MaterialIndex> getWritten() const { return mWritten.getSlots(); }

        /// The footprints `material` is asked at (`FootprintAsks`): what its textures are asked at, each by its own
        /// size and transform (`docs/architecture.md`, "A texture stands from the level its nearest user asks").
        const FootprintAsks& getAsks(const MaterialIndex material) const { return mAsks[material]; }
        const ArrivedRuns& getArrivedRuns() const { return mArrived; }

        /// How many runs have been placed, ever. What a backend checks it staged the arrivals
        /// against: a run is written on arrival and never again, so one that arrived where the
        /// backend was not told would be read stale for its life.
        std::uint64_t getRunRevision() const { return mRunRevision; }

        void clearArrivals();

    private:
        /// What `SceneDesc::holdMaterial` and `SceneDesc::drop` stand on, so every hold on a
        /// material is taken and given back in one place, and every write that crosses into the
        /// texture table or the placements wearing a material is made through the scene.
        friend class SceneDesc;

        /// Puts `material` in a slot, holding every texture it names on `textures`.
        MaterialIndex add(TextureTable& textures, const Material& material);

        /// Rewrites a material in place, keeping its slot and everything standing on it, and says
        /// whether what traversal is told about the surfaces wearing it changed — a fade crossing
        /// opaque does, a flipbook turning does not. What it names anew is held on `textures` and
        /// what it stops naming given back.
        bool set(TextureTable& textures, MaterialIndex material, const Material& what);

        /// One hold more on `material` at `tier`, moving the holds it takes on its textures to its best tier where that
        /// moved: a texture is wanted as much as the best of the materials naming it is.
        void hold(TextureTable& textures, MaterialIndex material, HoldTier tier);

        /// One ask more, and one fewer, of `material` at footprint `step`, asking its textures again where its finest
        /// step moved: a placement the walk stands wearing it asks the finest, a cell of the ring its own distance's.
        void ask(TextureTable& textures, MaterialIndex material, std::size_t step);
        void unask(TextureTable& textures, MaterialIndex material, std::size_t step);

        /// Gives one hold on `material` back, and frees it where that was the last: what it named
        /// goes back to `textures`, and its layer and mask runs to their allocators.
        void drop(TextureTable& textures, MaterialIndex material, HoldTier tier);

        /// Records that `slot`'s row was written, once however many times it is.
        void note(MaterialIndex slot);

        /// Every texture slot `material` names — its own maps, and every layer of its run. One
        /// walk, or a slot added to the hold and forgotten in the drop is freed under something
        /// that still stands on it. The maps are `Material`'s own list, because the table is not
        /// where a map is declared; the layers are here, because their run is this table's storage.
        template <class Visit>
        void forEachTexture(const Material& material, Visit visit) const
        {
            material.forEachTexture(visit);

            for (const MaterialLayer& layer : material.mLayers.in(getLayers()))
            {
                visit(TextureIndex(layer.mDiffuse));
                visit(TextureIndex(layer.mNormal));
            }
        }

        /// Takes and gives back those slots at `tier`. Only ever called in that pair, and `set` is why the order
        /// between them matters.
        void holdTextures(TextureTable& textures, const Material& material, HoldTier tier);
        void dropTextures(TextureTable& textures, const Material& material, HoldTier tier);

        /// The tier `material`'s holds on its textures state: its own best, and `Kept` while nothing holds it, the
        /// moment between its `add` and its first hold, as a material holds what it names from the moment it is.
        HoldTier textureTierOf(MaterialIndex material) const;

        /// Moves `material`'s holds on its textures from the tier they stated, `before`, to the one they state now.
        void retierTextures(TextureTable& textures, MaterialIndex material, HoldTier before);

        /// Calls `visit` with every texture slot `material` reads, the half log of what its texels and its transform
        /// lay on a unit of the mesh's coordinates, `0.5 log2(T w h)`, and the coarsest level any read of it takes:
        /// its maps through the material's transform, a map read by direction at minus infinity, as one of no known
        /// size is, and each layer through its own, never coarser than the level its chunk's bake reads it at
        /// (`groundLod`). Its own list beside `forEachTexture`'s, which holds what reads nothing too.
        template <class Visit>
        void forEachRead(const TextureTable& textures, const Material& material, Visit visit) const;

        /// Whether two rows of one material read the same slots at the same stretch, so a rewrite between them moves
        /// no ask (`forEachRead`).
        static bool readsAlike(const Material& one, const Material& other);

        /// Asks the textures of `material` at the levels its finest footprint `after` makes of them, and takes back
        /// those `before` made, each slot whose level the two agree on left as it stands. Nothing for "none".
        void askTextures(TextureTable& textures, const Material& material, std::optional<std::uint8_t> before,
            std::optional<std::uint8_t> after) const;

        /// Each material's footprints, parallel to the rows.
        RowVector<FootprintAsks, MaterialIndex> mAsks;

        /// Rows written since the last `clearArrivals` — a flipbook that is added and then
        /// rewritten on one frame is one row, not two.
        SlotSet<MaterialIndex> mWritten;

        ArrivedRuns mArrived;
        std::uint64_t mRunRevision = 0;

        /// A material's layers, and the weights a layer places — runs and not slots, because a
        /// terrain chunk's layer run is as long as the ground types under it.
        RunBuffer<MaterialLayer> mLayers;
        RunAllocator mMaskRuns;

        /// Each standing mask's digest by its run's offset, where `keepDigests` asked for them.
        boost::unordered_flat_map<std::uint32_t, Misc::DigestWords> mMaskDigests;
        bool mKeepsDigests = false;
    };
}
