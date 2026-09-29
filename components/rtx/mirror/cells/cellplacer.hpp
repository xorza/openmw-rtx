#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec2i>

#include <components/esm3/refnum.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/terrain/objectstorage.hpp>

#include "cellworld.hpp"
#include "held.hpp"

namespace Rtx
{
    class CellHolds;
    class SceneDesc;
    struct PreparedCell;

    /// What a gate that decided says of one reference: whether it stands.
    struct GateVerdict
    {
        ESM::RefNum mRefNum;
        bool mStands = false;
    };

    /// Where the cells the ring holds stand: which of their placements are in the top level, by
    /// the rings and the size rule, and how their ground shades, by the grid. `CellRing` decides
    /// what is prepared and adopted; this decides what of it stands. The ground's rows are adopted
    /// here too, because the ground is the one thing this stands that no model was read for.
    class CellPlacer
    {
    public:
        explicit CellPlacer(SceneDesc& scene)
            : mScene(scene)
        {
        }

        /// The size rule's constant: a reference is placed while its scaled radius is at least this
        /// times the eye's distance to its cell. `object paging min size`, told rather than asked
        /// because this library reads no settings.
        void setMinSize(float minSize) { mMinSize = minSize; }

        /// `[RTX] specular map layout`: whether a ground layer's `_diffusespec` is an authored
        /// albedo with its roughness in alpha, or the plain diffuse OpenMW swaps in. Told rather
        /// than asked, for the same reason.
        void setSpecularLayout(SpecularLayout layout) { mSpecularLayout = layout; }

        /// What the game says of one reference, which the content files cannot: a script has
        /// disabled it, or enabled it again. Applied to the cells `held` at once, because `place`
        /// walks only what the size rule changed since the last frame. Remembered for the cells not
        /// yet held, which arrive with the flag set.
        void setReferenceEnabled(ESM::RefNum refnum, bool enabled, std::span<HeldCell> held);

        /// The game moved, deleted or animates the reference, so it is never stood again whatever
        /// a script says of it afterwards: upstream's paging keeps the same list beside its
        /// disabled one, and a `setReferenceEnabled(refnum, true)` does not undo it. Dropped from
        /// the cells `held` at once, and kept out of the cells not yet held.
        void blacklistReference(ESM::RefNum refnum, std::span<HeldCell> held);

        /// What a visibility gate says of the references behind it, for the cells no one has
        /// loaded: stand, stand not, or stand by what the game says of each. Applied to the cells
        /// `held` at once, as a script's word is, and remembered for the cells not yet held. A
        /// gate never told keeps its references down.
        void setGate(std::uint32_t gate, Terrain::GateState state, std::span<HeldCell> held);

        /// Forgets everything a script said and everything the game blacklisted: the world is
        /// cleared for a new game or a saved one, and what was kept out of the old one stands in
        /// the new. Every reference kept out of the cells `held` stands again at once, as
        /// `setReferenceEnabled` would stand each.
        void forgetReferences(std::span<HeldCell> held);

        /// Adopts a cell's ground into the scene, on rows held on the scene. `around` says whether
        /// it shades from its stack.
        void adoptGround(const PreparedCell& cell, HeldCell& held, const WorldAround& around, ExtractionStats& stats);

        /// Fills `held.mPlacements` from the cell's references, one per part of each model as
        /// `holds` adopted it, disabled where a script said so, and sorted for `place`; and
        /// `held.mLights` from the cell's lamps, whole.
        void adoptPlacements(const PreparedCell& cell, HeldCell& held, CellHolds& holds);

        /// Lets a cell's ground go: its slot and its rows. The sweep after this walk is what frees
        /// the rows.
        void dropGround(HeldCell& cell);

        /// Takes a cell's placements out of the top level, keeping the cell.
        void dropSlots(HeldCell& cell);

        /// Places and drops one cell by the reach and the size rule, flattens its ground by the
        /// grid, and stands its lamps where the cell is in reach and outside the grid — the game's
        /// own graph carries the lamps inside it, and a lantern must not be counted twice.
        /// @return how many lamps were stood.
        std::uint32_t place(HeldCell& cell, const WorldAround& around);

        /// Appends a verdict for every reference of `cell` behind a gate that decided, once each.
        /// **Where the cell is active the game has run the reference's script**, so the verdict is
        /// what the script left it as, or the gate's run is not the script's first frame — and the
        /// distance shows a stage the cell takes down as it loads.
        void collectGateVerdicts(const HeldCell& cell, std::vector<GateVerdict>& into) const;

        /// How many statics and how many grounds stand in the top level.
        std::uint32_t getPlaced() const { return mPlaced; }
        std::uint32_t getGroundPlaced() const { return mGroundPlaced; }

        /// Whether `cell` stands exactly what its holding says, by the rings and the size rule as
        /// `around` stands now: every placement the rule admits and no script disabled has a slot,
        /// and that slot holds its mesh and material under the ring's own name; every other
        /// placement has none; and the ground stands in the reach and nowhere else. What `place`
        /// keeps, asked after it, for an assert.
        bool standsAsHeld(const HeldCell& cell, const WorldAround& around) const;

        /// Whether the top level holds exactly `getPlaced() + getGroundPlaced()` slots under the
        /// ring's name: a slot the ring stood and lost track of would stand for ever, and only a
        /// count of the table can see one.
        bool standsNoMore() const;

    private:
        /// Whether `refnum` is in the sorted list.
        static bool isListed(const std::vector<ESM::RefNum>& sorted, ESM::RefNum refnum);

        /// Calls `visit(placement, shown)` for every placement of the reference in the cells `held`.
        template <class Visit>
        void forEachPlacementOf(ESM::RefNum refnum, std::span<HeldCell> held, Visit visit);

        /// What `gate` said last, `Unknown` where it said nothing yet.
        Terrain::GateState stateOf(std::uint32_t gate) const;

        /// Whether nothing keeps the placement down: the blacklist, its gate, or a script's word.
        bool stands(const Placement& placement) const;

        /// Stands or drops the placement by `stands`, where the size rule has it `shown`.
        void restand(Placement& placement, bool shown);

        /// Puts `stood` in the top level under the ring's name and counts it in `standing`, and
        /// takes it out again. Dropping what does not stand is nothing.
        void stand(Stood& stood, std::uint32_t& standing);
        void drop(Stood& stood, std::uint32_t& standing);

        /// Whether a cell's ground, whose material is `ground`, wants its stack flattened where the
        /// eye stands now.
        static bool wantsFlattening(const osg::Vec2i& cell, const Material& ground, const WorldAround& around);

        SceneDesc& mScene;

        float mMinSize = 0.0f;

        SpecularLayout mSpecularLayout = SpecularLayout::Ignore;

        /// References a script has disabled, sorted.
        std::vector<ESM::RefNum> mDisabled;

        /// References the game blacklisted, sorted. Apart from the disabled, because a script may
        /// enable one of those again and never one of these.
        std::vector<ESM::RefNum> mBlacklisted;

        /// What each gate said last, `Unknown` past the last one told.
        std::vector<Terrain::GateState> mGates;

        std::uint32_t mPlaced = 0;
        std::uint32_t mGroundPlaced = 0;

        // Refilled per ground adopted.
        std::vector<MaterialLayer> mLayerScratch;
    };
}
