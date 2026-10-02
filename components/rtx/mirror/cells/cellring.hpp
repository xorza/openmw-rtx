#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <osg/Vec2i>
#include <osg/Vec3f>

#include <components/esm3/refnum.hpp>
#include <components/rtx/common/stepped.hpp>
#include <components/rtx/mirror/sceneadopter.hpp>
#include <components/rtx/scene/specularlayout.hpp>
#include <components/terrain/pagedcellref.hpp>

#include "cellplacer.hpp"
#include "cellsupply.hpp"
#include "cellworld.hpp"
#include "held.hpp"
#include "prepared.hpp"
#include "readermemory.hpp"

namespace Rtx
{
    /// The world's cells as this renderer stands them: their ground off the land records, and the
    /// statics of the cells the simulation does not hold as instances of their templates. Not
    /// `Terrain::QuadTreeWorld`'s chunks nor `Terrain::ObjectPaging`'s merges, which cut and fold
    /// geometry by the eye's distance on the frame that first meets it — two thirds of the tail a
    /// cell ring costs, measured. A ray tracer has no draw calls to save: a top level takes a
    /// thousand instances of one bottom level as one entry apiece, and a cell's own 65 × 65 grid
    /// meets its neighbours vertex for vertex.
    ///
    /// Two rings, and a thread between them. The *prepared* ring is the reach plus one band: each
    /// cell in it has been read by the thread and adopted by the frame into the scene, so its
    /// meshes stand on the device before anything can see them. The *placed* ring is the reach: a
    /// cell in it has its ground in the top level, and outside the active grid its statics too. A
    /// frame adopts at most one prepared cell and moves the placements of the cells that crossed a
    /// boundary. This is the policy; `CellHolds` is the bookkeeping of what the reader lent and
    /// the holds adoption took; `CellPlacer` is what stands and by what rule. A model's drawable
    /// is the template's, so a mesh adopted here is the mesh the frame's walk finds under the
    /// clone when the cell becomes active, and nothing is uploaded twice. The ground's rows are
    /// the ring's own: no drawable will ever name them, so the placer holds them on the scene. What
    /// this adopts goes through the extractor's own resolvers inside the walk (`SceneAdopter`),
    /// which is also the scene everything here stands in and the walk's counts it adds to, and
    /// holds by `Reach::mHolds` rather than being named again on every walk. Everything the
    /// thread reads is lent and given back — `Spares` says why an address and not a shared count,
    /// and `giveBackHolds` why a hold is a cell's.
    ///
    /// **The lamps of the cells the game has not loaded are the ring's too.** `REC_LIGH` is not a
    /// paged type and must not become one, because both renderers read the paging's type filter;
    /// what this fork cannot keep is the lamp, because rays go everywhere, and a town four cells
    /// away that goes dark at dusk is the world stating something the content files do not. Read
    /// with the cell's other records in one walk of them, off the frame: the light stood by
    /// `CellPlacer::place`, which says where, and the model as any static's.
    class CellRing
    {
    public:
        /// @param adopter what every row this stands is adopted through, and the scene it stands
        ///        them in — the extractor whose world walk collects this.
        explicit CellRing(SceneAdopter& adopter);

        CellRing(const CellRing&) = delete;
        CellRing& operator=(const CellRing&) = delete;

        /// Stops the thread. A cell in flight is finished and dropped rather than waited out.
        ~CellRing();

        /// Where the world is now, and what of it is read. A world with no content stands nothing,
        /// and a change of what is read drops everything held and starts again. A change of the
        /// day-night mode moves the switches of every cell held at once —
        /// `CellPlacer::setNightDay`. Told once a frame, before the walk.
        void follow(const WorldAround& around);

        /// Whether the ring stands the distance's statics at all. The ground stands either way;
        /// off is the A/B `--distant-statics=false` is, and a change of it reads every cell again.
        void setStaticsEnabled(bool enabled);

        void setMinSize(float minSize) { mPlacer.setMinSize(minSize); }

        /// What the ground's `_diffusespec` maps mean — `CellPlacer::setSpecularLayout`.
        void setSpecularLayout(SpecularLayout layout) { mPlacer.setSpecularLayout(layout); }

        /// The frame the next walk is for, so a frame walked twice adopts one cell and not two.
        /// Told by `SceneExtractor::extractWorld`, which is the call that has the ring and the
        /// frame both; a caller that drives a ring outside a walk says it itself.
        void setFrame(std::size_t frame);

        /// Whether a walk waits for the cell it is about to adopt, so which frame a cell is adopted
        /// on is the schedule's answer and not the thread's. The order is what makes it so: one
        /// reader takes the cells `ask` sorted and hands them back in that order. One cell a frame
        /// either way.
        void setSettled(bool settled);

        /// How many cells of its band the ring still has to stand after the last walk: asked for and
        /// not yet read, or read and waiting for the frame that adopts them. What a harness stop
        /// waits out before it measures, settled or not.
        std::uint32_t getCellsToStand() const;

        /// `CellSupply::getReaderMemory`.
        ReaderMemory getReaderMemory() { return mSupply.getReaderMemory(); }

        /// `CellPlacer::setReferenceEnabled`, `blacklistReference` and `forgetReferences`, over the
        /// cells held. Between frames, which the ring's own turn asserts: a reference changed
        /// between `follow` and the end of `collect` would land on cells the frame is placing.
        void setReferenceEnabled(ESM::RefNum refnum, bool enabled);
        void blacklistReference(ESM::RefNum refnum);
        void forgetReferences();

        /// `CellPlacer::setGate`, over the cells held, between frames as the three above are.
        void setGate(std::uint32_t gate, Terrain::GateState state);

        /// Hands the adopter everything held that the graph does not parent, and adds what it stood
        /// to the walk's counts, because the ring is stood inside the adopter's walk.
        void collect();

        /// Gives the adopter back every hold `forget` let go of, for a ring the frame will not walk
        /// again — a world detached. `collect` does the same at both ends of a walk.
        void releaseHolds() { mHolds.releaseParts(mAdopter); }

        /// Appends the reference number of every static standing in the top level, for a check
        /// that asks the game whether it stands the same one.
        void collectStanding(std::vector<ESM::RefNum>& into) const;

        /// Appends what every gate that decided says of each reference behind it in a cell of the
        /// active grid, for a check that asks the game whether its own scripts left each the same
        /// way — `CellPlacer::collectGateVerdicts`.
        void collectGateVerdicts(std::vector<GateVerdict>& into) const;

        // Read by the tests and by nothing else.
        /// How many cells the prepared ring holds.
        std::size_t getHeldCellCount() const { return mPlacer.getHeldCount(); }

    private:
        bool handed(const osg::Vec2i& cell) const;

        /// Moves the supply's finished cells into the frame's own list, counting their models. A
        /// cell read with the statics the other way is let go of here.
        void takeDone();

        /// Hands the supply the cells the prepared disc lacks, nearest first. `band` is the disc's
        /// radius in units, which is the reach and the prepared band past it.
        void ask(const osg::Vec3f& eye, float band);

        /// Gives back every handed cell outside the band. Run after every `takeDone`, because a cell
        /// handed over during the wait was asked for from where the eye stood before.
        void sift(const osg::Vec3f& eye, float band);

        /// Blocks until the supply has read a cell this walk can adopt (`setSettled`), or until the
        /// reader has nothing left to read, which returns with nothing handed. Only where the last
        /// `ask` named something, because nothing is coming otherwise.
        void waitForNext(const osg::Vec3f& eye, float band);

        /// Adopts the next cell the supply read, which is one cell and one frame's worth.
        void adoptHanded();

        void adopt(PreparedCell& cell);

        /// Lets go of a handed cell the frame will not adopt.
        void discard(PreparedCell& cell);

        /// Hands the supply a cell's holds on its models and its ground's textures, for a cell the
        /// frame is letting go of — one it adopted, or one handed over that it never did. A hold
        /// is a cell's, which is what makes a return exact whatever the thread read in between.
        void giveBackHolds(const HeldCell& cell);
        void giveBackHolds(const PreparedCell& cell);

        /// What a held cell the ring lets go of owes: its hold on each model, and the reader's
        /// holds on its models and its ground's images.
        void letGo(const HeldCell& cell);

        /// The whole of what a walk does to the rings: what `collect` wraps in the walk it is inside.
        void walkRings(ExtractionStats& stats);

        /// Lets go of every cell, every model and every image the frame holds. What the supply lent
        /// dies with its reader, so this runs before the supply is pointed anywhere else.
        void forget();

        SceneAdopter& mAdopter;

        /// The cells themselves, read on a thread of its own.
        CellSupply mSupply;

        /// The models and images lent, and what they were adopted as.
        CellHolds mHolds;

        /// The cells held, and what of them stands by what rule.
        CellPlacer mPlacer;

        /// Where the eye is and how much world there is around it, as the last `follow` said.
        WorldAround mAround;

        /// Where the ring stands in a frame: told where the world is, or walked. `follow` comes
        /// before `collect` — "told once a frame, before the walk" — and a walk that was not told
        /// is asserted where it happens rather than standing last frame's rings.
        enum class Turn
        {
            Followed,
            Collected,
        };

        Stepped<Turn> mTurn{ Turn::Collected };

        bool mStatics = true;
        bool mSettled = false;

        /// What a request is made from. `ask` rebuilds the whole band, and nothing else on the frame
        /// path is proportional to the band, so it rebuilds where any of these differs from the last
        /// request's and nowhere else: a function of its inputs, where a flag set at each place an
        /// input moves missed the reach, which grows under a still eye.
        struct AskInputs
        {
            osg::Vec3f mEye;
            float mBand = 0.0f;
            bool mStatics = true;
            std::size_t mHeld = 0;
            std::size_t mHanded = 0;

            /// How many cells were ever taken from the supply, and how many worlds followed: a cell
            /// discarded on arrival and a new reader change no set, and are asked for again.
            std::uint64_t mTaken = 0;
            std::uint64_t mFollowed = 0;

            bool operator==(const AskInputs& other) const = default;
        };

        std::optional<AskInputs> mAsked;
        std::uint64_t mTaken = 0;
        std::uint64_t mFollowed = 0;

        /// How many cells the band held when `ask` last walked it, the reach and the band past it,
        /// or nought where the last walk stood nothing — indoors, or with no reader. Every held cell
        /// of an exterior walk is one of them once it has dropped what left, so what is left to stand
        /// is this less what is held.
        std::uint32_t mBandCells = 0;

        std::size_t mFrame = 0;

        /// The frame a cell was last adopted on, so a frame walked twice adopts once.
        std::size_t mAdoptedFrame = ~std::size_t{ 0 };

        /// What the thread read and handed over, and the frame has not adopted yet, in the order it
        /// arrived.
        std::vector<PreparedCell*> mHanded;

        // Refilled per frame.
        std::vector<PreparedCell*> mDoneScratch;

        /// What the supply is asked for, refilled per walk and never freed.
        CellRequest mAsking;
    };
}
