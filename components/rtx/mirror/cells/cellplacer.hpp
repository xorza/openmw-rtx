#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <boost/container/flat_set.hpp>
#include <osg/Vec2i>
#include <osg/Vec4i>

#include <components/esm3/refnum.hpp>
#include <components/rtx/common/scratch.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/mirror/extractionstats.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/terrain/objectstorage.hpp>

#include "cellworld.hpp"
#include "held.hpp"
#include "nightday.hpp"

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

    /// The cells the ring holds, and where they stand: which of their placements are in the top
    /// level, by the rings and the size rule, and how their ground shades, by the grid. `CellRing`
    /// decides what is prepared and adopted; this holds it and decides what of it stands. The
    /// ground's rows are adopted here too, because the ground is the one thing this stands that no
    /// model was read for.
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
        /// disabled it, or enabled it again. Applied to the cells held at once, its placements and
        /// its lamp's light, because `place` walks only what the size rule changed since the last
        /// frame. Remembered for the cells not yet held, which arrive with the flag set.
        void setReferenceEnabled(ESM::RefNum refnum, bool enabled);

        /// The game moved, deleted or animates the reference, so it is never stood again whatever
        /// a script says of it afterwards: upstream's paging keeps the same list beside its
        /// disabled one, and a `setReferenceEnabled(refnum, true)` does not undo it. Dropped from
        /// the cells held at once, and kept out of the cells not yet held.
        void blacklistReference(ESM::RefNum refnum);

        /// What a visibility gate says of the references behind it, for the cells no one has
        /// loaded: stand, stand not, or stand by what the game says of each. Applied to the cells
        /// held at once, as a script's word is, and remembered for the cells not yet held. A gate
        /// never told keeps its references down.
        void setGate(std::uint32_t gate, Terrain::GateState state);

        /// Which child the world's day-night switches show, for the placements a switch shows in
        /// some modes and not others. Applied to the cells held at once, as a gate is, and
        /// remembered for the cells not yet held.
        void setNightDay(NightDayMode mode);

        /// Forgets everything a script said and everything the game blacklisted: the world is
        /// cleared for a new game or a saved one, and what was kept out of the old one stands in
        /// the new. Every reference kept out of the cells held stands again at once, as
        /// `setReferenceEnabled` would stand each.
        void forgetReferences();

        /// Holds `cell` from now on, its ground adopted into the scene on rows held on the scene,
        /// and returns it for its models to be adopted into. `around` says whether the ground
        /// shades from its stack. Nothing stands until `adoptPlacements` and a `place`.
        HeldCell& hold(const PreparedCell& cell, const WorldAround& around, ExtractionStats& stats);

        /// Fills `held.mPlacements` from the cell's references, one per part of each model as
        /// `holds` adopted it, disabled where a script said so, and sorted for `place`; and
        /// `held.mLights` from the cell's lamps, with what the game said of each.
        void adoptPlacements(const PreparedCell& cell, HeldCell& held, CellHolds& holds);

        /// Whether a cell at `cell` is held.
        bool holds(const osg::Vec2i& cell) const { return mCells.contains(cell); }

        /// Lets go of every held cell `keep` refuses: its placements out of the top level, then
        /// `letGo(cell)` — for what the ring lent it — then its ground and its rows, whose sweep
        /// after this walk frees them. The cell's vectors are kept for the next one.
        /// @return how many cells went.
        template <class Keep, class LetGo>
        std::size_t dropUnless(Keep keep, LetGo letGo);

        /// Takes every held cell's placements out of the top level, keeping the cells.
        void dropSlots();

        /// Places and drops every held cell by the reach and the size rule, flattens its ground by
        /// the grid, and stands the lamps nothing the game says keeps down, where the cell is in
        /// reach and outside the grid — the game's own graph carries the lamps inside it, and a
        /// lantern must not be counted twice.
        /// @return how many lamps were stood.
        std::uint32_t place(const WorldAround& around);

        /// Appends the reference number of every static standing in the top level.
        void collectStanding(std::vector<ESM::RefNum>& into) const;

        /// Appends a verdict for every reference behind a gate that decided, once each per cell,
        /// in every held cell of the active grid. **Where the cell is active the game has run the
        /// reference's script**, so the verdict is what the script left it as, or the gate's run
        /// is not the script's first frame — and the distance shows a stage the cell takes down as
        /// it loads.
        void collectGateVerdicts(const osg::Vec4i& activeGrid, std::vector<GateVerdict>& into) const;

        /// How many statics and how many grounds stand in the top level.
        std::uint32_t getPlaced() const { return mPlaced; }
        std::uint32_t getGroundPlaced() const { return mGroundPlaced; }

        std::size_t getHeldCount() const { return mCells.size(); }

        /// Whether the top level holds exactly what the held cells say it should, by the rings and
        /// the size rule as `around` stands now: every placement the rule admits and no script
        /// disabled has a slot, and that slot holds its mesh and material under the ring's own
        /// name; every other placement has none; the ground stands in the reach and nowhere else;
        /// and no slot under the ring's name is one no cell holds. What `place` keeps, asked after
        /// it, for an assert.
        bool standsAsHeld(const WorldAround& around) const;

    private:
        /// What the cell table is ordered by, stated once so that no search can disagree with the
        /// insertion it is looking for. `osg::Vec2i` orders lexicographically already, which is the
        /// order a walk over the held cells wants: the same walk on every machine.
        struct CellAt
        {
            const osg::Vec2i& operator()(const HeldCell& held) const { return held.mCell; }
        };

        /// Adopts a cell's ground into the scene, on rows held on the scene.
        void adoptGround(const PreparedCell& cell, HeldCell& held, const WorldAround& around, ExtractionStats& stats);

        /// Lets a cell's ground go: its slot and its rows.
        void dropGround(HeldCell& cell);

        /// Takes a cell's placements out of the top level, keeping the cell.
        void dropSlots(HeldCell& cell);

        std::uint32_t place(HeldCell& cell, const WorldAround& around);

        void collectGateVerdicts(const HeldCell& cell, std::vector<GateVerdict>& into) const;

        /// `standsAsHeld` for one cell.
        bool standsAsHeld(const HeldCell& cell, const WorldAround& around) const;

        /// Whether the top level holds exactly `getPlaced() + getGroundPlaced()` slots under the
        /// ring's name: a slot the ring stood and lost track of would stand for ever, and only a
        /// count of the table can see one.
        bool standsNoMore() const;

        /// Whether `refnum` is in the sorted list.
        static bool isListed(const std::vector<ESM::RefNum>& sorted, ESM::RefNum refnum);

        /// Calls `visit(placement, shown)` for every placement in the cells held that `match`es.
        template <class Match, class Visit>
        void forEachPlacementWhere(Match match, Visit visit);

        /// Calls `change(state)` for every reference state in the cells held that `match`es, a
        /// placement's and a lamp's, and restands each placement changed.
        template <class Match, class Change>
        void changeReferencesWhere(Match match, Change change);

        /// The same for the states of `refnum` alone, found through each cell's
        /// `HeldCell::mByReference`.
        template <class Change>
        void changeReference(ESM::RefNum refnum, Change change);

        /// What `gate` said last, `Unknown` where it said nothing yet.
        Terrain::GateState stateOf(std::uint32_t gate) const;

        /// Whether nothing keeps the placement down: the day-night mode, or what the game says of
        /// its reference.
        bool stands(const Placement& placement) const;

        /// Whether nothing the game says keeps the reference down: the blacklist, its gate, or a
        /// script's word. The one rule for a placement and a lamp's light.
        bool standsBy(const ReferenceState& reference) const;

        /// What the game has said of `refnum` so far, for a reference of a cell being adopted.
        ReferenceState heard(ESM::RefNum refnum, std::uint32_t gate) const;

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

        NightDayMode mNightDay = NightDayMode::Default;

        std::uint32_t mPlaced = 0;
        std::uint32_t mGroundPlaced = 0;

        // Refilled per ground adopted.
        std::vector<MaterialLayer> mLayerScratch;

        /// The cells held, in `CellAt`'s order, and the room a dropped cell's vectors grew.
        boost::container::flat_set<HeldCell, KeyedLess<osg::Vec2i, CellAt>, std::vector<HeldCell>> mCells;
        Recycled<HeldCell> mSpareCells;
    };

    template <class Keep, class LetGo>
    std::size_t CellPlacer::dropUnless(Keep keep, LetGo letGo)
    {
        // Erased one by one and not `erase_if`, because a cell that goes is given back first, which
        // a remove's predicate may not do to its row. A band is a hundred or so cells and a
        // crossing drops a few, so the shifts are nobody's concern.
        std::size_t dropped = 0;
        for (auto cell = mCells.begin(); cell != mCells.end();)
        {
            if (keep(*cell))
            {
                ++cell;
                continue;
            }

            dropSlots(*cell);
            letGo(*cell);
            dropGround(*cell);
            cell->reuse();
            mSpareCells.give(std::move(*cell));
            cell = mCells.erase(cell);
            ++dropped;
        }

        return dropped;
    }
}
