#pragma once

#include <span>
#include <vector>

#include <osg/Matrixf>

#include <components/rtx/common/runs.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/shaders/scene.h>

#include "material.hpp"
#include "mesh.hpp"

namespace Rtx
{
    /// A slot's place in one list of slots threaded through the table, doubly linked so a drop
    /// leaves it in constant time.
    struct PlacementLinks
    {
        Index mNext = sNoIndex;
        Index mPrevious = sNoIndex;
    };

    /// One slot of the table: the placement, and everything the table knows about the slot beside
    /// it. One row and not a row with arrays beside it, so nothing can fall out of step with the
    /// slot it describes.
    struct PlacementRow
    {
        MeshInstance mInstance;

        /// Where the slot stood before the last `advance`, which is what a motion vector is the
        /// difference of.
        osg::Matrixf mPrevious;

        /// What traversal is told about the material the placement wears, as it stood when the
        /// placement was stood or the material last rewritten. Kept here so a fade, a drop and a
        /// record read one answer without the material table; a material rewrite brings it here
        /// through `rewriteWearing`. Default for a placement wearing nothing.
        Material::Traversed mWorn;

        /// The slots wearing the same material, headed by `PlacementTable::mFirstWearing`, so a
        /// material rewrite reaches its wearers alone.
        PlacementLinks mWearing{};

        /// The slots placing the same mesh, headed by `PlacementTable::mFirstPlacing`, so a
        /// backend that moved a mesh's structure rewrites its placements alone.
        PlacementLinks mPlacing{};
    };

    /// Where every mesh stands, where it stood, what each counts as and which rows a backend has
    /// to write again. A slot is never moved and never closed up, because a hit reads its slot
    /// index back. The two change lists are the whole of what a backend rewrites: a world is tens
    /// of thousands of placements and a frame changes hundreds. An arrival takes the lowest free
    /// slot and never the last one freed, because the slot settles a tie between coincident sheets
    /// and the order a sweep drops slots in is a hash of node addresses.
    ///
    /// **The counts are kept here, by the row that changed.** What a placement counts as is
    /// decided by its material's traversal facts, its class and its opacity, all of which this
    /// table is the last to see; a backend that recounted from the rows it was handed kept a
    /// second flag per row to take a departed row's share back, and this table still has the row.
    class PlacementTable
    {
    public:
        /// Fades the placement in `slot`. Separate from `move`, because an actor fading on the spot
        /// has not moved; a fade that changed the number joins `getMoved` all the same, as a row to
        /// rewrite that carries no motion. The walk's alone: the ring stands and drops, and never
        /// fades.
        void fade(Index slot, float opacity);

        /// Moves the placement in `slot`, and says whether that changed anything. A transform equal
        /// to the one already there writes nothing, which is the ordinary case. The walk's alone,
        /// as `fade` is.
        bool move(Index slot, const osg::Matrixf& transform);

        /// Says every row wearing `material` now wears `worn` and has to be written again — the
        /// material changed what traversal is told about the surfaces standing on it. The
        /// placements that wear it and no other: a list per material is threaded through the
        /// slots, so a fade crossing opaque costs its own placements rather than a walk of the
        /// world's.
        void rewriteWearing(Index material, const Material::Traversed& worn);

        /// Calls `visit` with every slot placing `mesh`, newest first. The placements of that mesh
        /// and no other, which is what a backend whose structure for it moved rewrites: a walk of
        /// every row was a cache miss a row, on every placement that compacted anything.
        template <class Visit>
        void forEachPlacing(const Index mesh, Visit&& visit) const
        {
            if (mesh >= mFirstPlacing.size())
                return;

            for (Index slot = mFirstPlacing[mesh]; slot != sNoIndex; slot = mRows.at(slot).mPlacing.mNext)
                visit(slot);
        }

        /// Ends a placement a backend took: what moved becomes where things were. Costs what moved
        /// and not what stands. What was moved becomes `getSettled`, and `getMoved` starts empty —
        /// so only after a backend has read both, because a slot that leaves the lists unread is a
        /// row no copy of the tables is ever told about.
        void advance();

        /// Every slot, standing or empty, in slot order. `MeshInstance::isPlaced` tells them apart.
        std::span<const PlacementRow> getRows() const { return mRows.getRows(); }

        /// How many slots hold a placement and how many of those each kind of traversal has to
        /// stop for, as the rows stand now.
        const InstanceCounts& getCounts() const { return mCounts; }

        /// The slots whose row changed since the last `advance`: placed, moved, faded, dropped, or
        /// wearing a material that changed what traversal is told. Each slot once.
        std::span<const Index> getMoved() const { return mMoved.getSlots(); }

        /// The slots the last `advance` caught up, whose motion is now still — the other half of
        /// what a backend rewrites, or last frame's motion would stay in the row for ever.
        std::span<const Index> getSettled() const { return mSettled.getSlots(); }

        /// The standing slots a walk along the eye's ray looks for — a medium or an additive
        /// surface, `InstanceCounts::mMedium` and `mAdditive` — kept as the counts are, by the row
        /// that changed.
        std::span<const Index> getPresent() const { return mPresent.getSlots(); }

        /// Where each of `getPresent`'s placements can be met, as a sphere about the box of its
        /// mesh carried through its transform, into `into`: cleared, then one row a slot whose mesh
        /// has a box. A mesh with none has no vertices yet, and nothing of it can be met; the
        /// spheres are described again at every placement, so the box is read once it arrives.
        ///
        /// @param meshes the scene's, whose boxes a placement names.
        void describePresences(std::span<const MeshRange> meshes, std::vector<Shaders::GpuPresence>& into) const;

    private:
        /// The two ways in and out, `SceneDesc::addInstance` and `SceneDesc::dropInstance`, which
        /// hold and give back the placement's mesh and material around them.
        friend class SceneDesc;

        /// Puts `instance` in a free slot, or in a new one, and returns it.
        Index add(const MeshInstance& instance, const Material::Traversed& worn);

        /// Empties `slot`. Its index is not reused until the next `add` asks for one. The slot
        /// joins `getMoved`: a backend has to write its row inactive, or the structure goes on
        /// tracing what stood there.
        ///
        /// @param by who is dropping it, which must be who stood it — `Stander`.
        void drop(Index slot, Stander by);

        /// What a standing row counts as — one placed, and one of each figure its material and its
        /// class put it in. The traversal figures are `PlacedTraversal`'s, which `InstanceRecord`'s
        /// flags read too, so a row is counted as what traversal is told it is.
        static InstanceCounts shareOf(const PlacementRow& row);

        /// Adds the row's share to the counts, and takes it back again. The row is read both
        /// times: every change to what it counts as takes it out first and puts it back after.
        void count(Index slot);
        void discount(Index slot);

        /// Puts `slot` at the head of the list `heads` keeps for `key` and threads through `links`,
        /// and takes it out again. Nothing for a key of `sNoIndex`, which is a placement wearing no
        /// material.
        void link(Index slot, Index key, std::vector<Index>& heads, PlacementLinks PlacementRow::*links);
        void unlink(Index slot, Index key, std::vector<Index>& heads, PlacementLinks PlacementRow::*links);

        SlotRows<PlacementRow> mRows;

        /// The newest placement wearing each material, and the newest placing each mesh,
        /// `sNoIndex` where none does. Parallel to the materials and the meshes, and grown with
        /// them.
        std::vector<Index> mFirstWearing;
        std::vector<Index> mFirstPlacing;

        /// Each slot once, however many of the facts about it changed: a slot named twice is its
        /// record made, a four-by-four inverse and a row in both device tables, twice.
        SlotSet mMoved;
        SlotSet mSettled;

        InstanceCounts mCounts;

        /// Never stale: a slot taken out is compacted away at once, because one that leaves and
        /// comes back — a fade counts a row out and in — would otherwise stand in the list twice.
        /// A handful of slots, so the pass costs nothing.
        SlotSet mPresent;
    };
}
