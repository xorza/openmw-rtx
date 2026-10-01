#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <boost/container/flat_set.hpp>
#include <osg/Matrixf>
#include <osg/Vec2i>
#include <osg/Vec3f>

#include <components/esm3/refnum.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/common/scratch.hpp>
#include <components/rtx/common/slots.hpp>
#include <components/rtx/mirror/sceneadopter.hpp>
#include <components/rtx/scene/rowhold.hpp>
#include <components/sceneutil/lightcommon.hpp>
#include <components/terrain/pagedcellref.hpp>

#include "nightday.hpp"
#include "prepared.hpp"

namespace osg
{
    class Drawable;
    class StateSet;
}

namespace Rtx
{
    /// One thing the ring may stand in the top level: what it is, where, and the slot it holds
    /// while it stands. The one shape a static and a cell's ground share, so one call stands
    /// either and one call drops it.
    struct Stood
    {
        Index mMesh = sNoIndex;
        Index mMaterial = sNoIndex;
        osg::Matrixf mTransform;

        /// The slot it stands in, or none while the size rule, a script or the ring keeps it out.
        Index mSlot = sNoIndex;

        bool isStanding() const { return mSlot != sNoIndex; }
    };

    /// What the game says of one reference the ring holds, which the content files cannot. One for
    /// each placement of it and one for its lamp, so a lamp's light hears what its model hears —
    /// `CellPlacer::standsBy`.
    struct ReferenceState
    {
        ESM::RefNum mRefNum;

        /// The gate the game decides its standing by, or `Terrain::sNoGate` — `CellPlacer::setGate`.
        std::uint32_t mGate = Terrain::sNoGate;

        /// A script has disabled the reference. Set as the cell is adopted and flipped by
        /// `CellPlacer::setReferenceEnabled`, which is what keeps it off the walk every frame makes.
        /// Behind a gate that decided, the gate's answer stands in for it.
        bool mDisabled = false;

        /// The game moved, deleted or animates the reference: nothing of it stands, whatever a
        /// script or a gate says.
        bool mBlacklisted = false;
    };

    /// One placement the ring may stand: a part of a model at a reference.
    struct Placement
    {
        Stood mStood;

        /// What the size rule reads. A cell's placements are sorted by it, largest first, so what
        /// the rule admits at any threshold is a prefix — `HeldCell::mShown`.
        float mRadius = 0.0f;

        /// The modes its part is shown in — `PreparedPart::mModes`.
        NightDayModes mModes;

        ReferenceState mState;
    };

    /// Where one reference is met in its cell: a placement's slot in `HeldCell::mPlacements`, or,
    /// counted on past them, a lamp's in `HeldCell::mLights`.
    struct ReferenceSpot
    {
        ESM::RefNum mRefNum;
        std::uint32_t mAt = 0;
    };

    /// One lamp's light the ring may stand: what `CellPlacer::place` builds a light from on every
    /// walk, and what the game says of the lamp.
    struct HeldLight
    {
        osg::Vec3f mPosition;
        SceneUtil::LightCommon mRecord;
        ReferenceState mState;
    };

    /// A cell's ground as the frame holds it: its rows, where it stands, and the readings of its
    /// textures. Its own type, because they are empty together. How many layers it has and whether
    /// it is flattened are its material row's, which is read and not copied.
    struct HeldGround
    {
        /// Whether the land stands anything, and so whether the rows below name anything. A flag
        /// and not an empty optional, so the texture list keeps its room from one cell to the next.
        bool mStands = false;

        /// The rows the ring stands, which no drawable and no state set will ever name, at the
        /// cell's centre, which the mesh's own positions are relative to.
        Stood mStood;

        /// The ring's holds on those rows, which `CellPlacer::dropGround` gives back.
        MeshHold mMeshHold;
        MaterialHold mMaterialHold;

        /// The readings of its textures, held for as long as the cell is.
        std::vector<PreparedTexture*> mTextures;

        /// Empties it for the next cell, keeping the room the texture list grew.
        void reuse() { reuseKeeping(*this, &HeldGround::mTextures); }
    };

    /// A cell the frame has adopted. Its vectors are kept when it is dropped, so a cell that arrives
    /// later refills them rather than growing new ones — `Recycled`.
    struct HeldCell
    {
        osg::Vec2i mCell;

        /// Whether its references were read, so a cell read under the other setting is dropped and
        /// read again.
        bool mStatics = false;

        /// Largest radius first, which `CellPlacer::adoptPlacements` sorts once.
        std::vector<Placement> mPlacements;

        /// How many of `mPlacements` the size rule admitted on the last `CellPlacer::place`: every
        /// placement before this stands unless a script disabled it, and none after it does. What a
        /// walk touches is the two ends of this and not the vector.
        std::size_t mShown = 0;

        std::vector<PreparedModel*> mModels;

        /// The ground, which stands nothing where the land names none.
        HeldGround mGround;

        /// The cell's lamps, which `CellPlacer::place` stands on every walk at the frame's own hour.
        std::vector<HeldLight> mLights;

        /// Every placement and every lamp by its reference, sorted by the reference and then by
        /// where it is met, so what the game says of one reference reaches that reference's own
        /// and not every row of every cell held.
        std::vector<ReferenceSpot> mByReference;

        /// Empties it for the next cell, keeping the room every list grew.
        void reuse()
        {
            // Nothing may stand: a spare that still stood would stand twice on the next adopt.
            // `CellPlacer::dropSlots` is what every caller runs first.
            assert(std::none_of(mPlacements.begin(), mPlacements.end(),
                       [](const Placement& placement) { return placement.mStood.isStanding(); })
                && !mGround.mStood.isStanding() && "a cell reused with something still standing");

            mCell = osg::Vec2i();
            mStatics = false;
            mShown = 0;
            mPlacements.clear();
            mModels.clear();
            mLights.clear();
            mByReference.clear();
            mGround.reuse();
        }
    };

    /// What the frame holds of the models and the images the reader lent it, and what it adopted
    /// them as — the bookkeeping half of the cell ring. The count here is the frame's, and the
    /// reader keeps one of its own, because it lends to cells the frame has not seen yet.
    class CellHolds
    {
    public:
        /// One part of a model as the scene holds it, and what it was adopted under — kept here
        /// rather than read off the model at release, because a world that is detached lets go of
        /// its models before the walk that could release them runs. Raw, because the identity maps
        /// own both for as long as the holds stand.
        struct AdoptedPart
        {
            Index mMesh = sNoIndex;
            Index mMaterial = sNoIndex;

            const osg::Drawable* mDrawable = nullptr;
            const osg::StateSet* mKey = nullptr;
        };

        /// A model the frame knows of: named by a cell it holds or by one the reader has handed
        /// over and it has not adopted. The frame forgets a model when no cell of either kind
        /// names it.
        struct HeldModel
        {
            PreparedModel* mModel = nullptr;

            /// Empty until the first cell naming the model is adopted.
            std::vector<AdoptedPart> mParts;

            /// Cells that name it, adopted or handed over and waiting.
            std::uint32_t mNamed = 0;
        };

        /// The entry for `model`, made where the frame knows of none.
        HeldModel& know(PreparedModel& model);

        /// The entry for a model the frame knows of.
        HeldModel& knownOf(const PreparedModel& model);

        /// Adopts a model's parts into the scene, where a cell first stands it. One hold per part on
        /// the mesh and on the material, which `release` gives back.
        void adoptParts(HeldModel& held, SceneAdopter& into);

        /// Counts one cell of `model` off, and forgets the model where none is left: its parts'
        /// holds go back on the next `releaseParts`.
        void release(PreparedModel& model);

        /// Gives the holds of every part `release` and `forget` let go of back to the walk,
        /// deferred because a release can happen outside one and only the walk can reach the
        /// resolvers. Run at both ends of a walk.
        void releaseParts(SceneAdopter& into);

        /// Lets go of every model, keeping their parts' holds for `releaseParts`. What the reader
        /// lent dies with it, so nothing is given back here.
        void forget();

    private:
        /// What the table is ordered by, stated once so that no search can disagree with the
        /// insertion it is looking for.
        struct ModelAt
        {
            const PreparedModel* operator()(const HeldModel& held) const { return held.mModel; }
        };

        boost::container::flat_set<HeldModel, KeyedLess<const PreparedModel*, ModelAt>, std::vector<HeldModel>> mModels;

        /// A model's row taken back out is room the next one refills rather than a heap call on
        /// the frame a cell lands.
        Recycled<HeldModel> mSpareModels;

        /// Parts let go of and not yet released to the walk. See `releaseParts`.
        std::vector<AdoptedPart> mReleasing;
    };
}
