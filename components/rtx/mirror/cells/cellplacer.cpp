#include "cellplacer.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>

#include <osg/Matrixf>
#include <osg/Vec3f>

#include <components/rtx/common/result.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/preprocess/shape/shapefold.hpp>
#include <components/rtx/scene/lightbuilder.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/surface.hpp>
#include <components/rtx/shaders/scene.h>

#include "cellgrid.hpp"
#include "held.hpp"
#include "prepared.hpp"

namespace Rtx
{
    void CellPlacer::setReferenceEnabled(const ESM::RefNum refnum, const bool enabled)
    {
        const auto at = std::lower_bound(mDisabled.begin(), mDisabled.end(), refnum);
        const bool known = at != mDisabled.end() && *at == refnum;

        if (enabled && known)
            mDisabled.erase(at);
        else if (!enabled && !known)
            mDisabled.insert(at, refnum);

        changeReference(refnum, [&](ReferenceState& state) { state.mDisabled = !enabled; });
    }

    void CellPlacer::blacklistReference(const ESM::RefNum refnum)
    {
        const auto at = std::lower_bound(mBlacklisted.begin(), mBlacklisted.end(), refnum);
        if (at != mBlacklisted.end() && *at == refnum)
            return;

        mBlacklisted.insert(at, refnum);
        changeReference(refnum, [](ReferenceState& state) { state.mBlacklisted = true; });
    }

    void CellPlacer::setGate(const std::uint32_t gate, const Terrain::GateState state)
    {
        assert(gate != Terrain::sNoGate && "a gate told of that is none");
        if (gate >= mGates.size())
            mGates.resize(gate + 1, Terrain::GateState::Unknown);
        mGates[gate] = state;

        changeReferencesWhere(
            [&](const ReferenceState& reference) { return reference.mGate == gate; }, [](ReferenceState&) {});
    }

    void CellPlacer::setNightDay(const NightDayMode mode)
    {
        if (mode == mNightDay)
            return;
        mNightDay = mode;

        // One frame restands every switched placement held, as the game turns every switch of its
        // own cells on that frame.
        forEachPlacementWhere([](const Placement& placement) { return !placement.mModes.isEvery(); },
            [&](Placement& placement, bool shown) { restand(placement, shown); });
    }

    template <class Match, class Visit>
    void CellPlacer::forEachPlacementWhere(Match match, Visit visit)
    {
        // Every cell, because none is indexed by what is asked: a gate and the day-night mode each
        // move rarely enough that the walk is cheaper than an index kept for it. A reference is
        // indexed, because the game blacklists one for every object a script moves.
        for (HeldCell& cell : mCells)
            for (std::size_t slot = 0; slot < cell.mPlacements.size(); ++slot)
                if (match(cell.mPlacements[slot]))
                    visit(cell.mPlacements[slot], slot < cell.mShown);
    }

    template <class Match, class Change>
    void CellPlacer::changeReferencesWhere(Match match, Change change)
    {
        forEachPlacementWhere([&](const Placement& placement) { return match(placement.mState); },
            [&](Placement& placement, bool shown) {
                change(placement.mState);
                restand(placement, shown);
            });

        // Nothing to restand: `place` builds every lamp's light again on every walk, and reads
        // the state then.
        for (HeldCell& cell : mCells)
            for (HeldLight& lamp : cell.mLights)
                if (match(lamp.mState))
                    change(lamp.mState);
    }

    template <class Change>
    void CellPlacer::changeReference(const ESM::RefNum refnum, Change change)
    {
        for (HeldCell& cell : mCells)
        {
            const auto [first, last]
                = std::ranges::equal_range(cell.mByReference, refnum, std::less<>{}, &ReferenceSpot::mRefNum);
            for (const ReferenceSpot& spot : std::ranges::subrange(first, last))
            {
                if (spot.mAt >= cell.mPlacements.size())
                {
                    change(cell.mLights[spot.mAt - cell.mPlacements.size()].mState);
                    continue;
                }

                Placement& placement = cell.mPlacements[spot.mAt];
                change(placement.mState);
                restand(placement, spot.mAt < cell.mShown);
            }
        }
    }

    void CellPlacer::forgetReferences()
    {
        mDisabled.clear();
        mBlacklisted.clear();

        changeReferencesWhere([](const ReferenceState& state) { return state.mDisabled || state.mBlacklisted; },
            [](ReferenceState& state) {
                state.mDisabled = false;
                state.mBlacklisted = false;
            });
    }

    bool CellPlacer::stands(const Placement& placement) const
    {
        return placement.mModes.has(mNightDay) && standsBy(placement.mState);
    }

    bool CellPlacer::standsBy(const ReferenceState& reference) const
    {
        if (reference.mBlacklisted)
            return false;
        if (reference.mGate == Terrain::sNoGate)
            return !reference.mDisabled;

        // A gate that decided is the game's answer for the cells it has not loaded, and a script's
        // word on the reference is an answer the gate has since moved past: the stage a visit
        // took down stands again from afar once the story reaches it.
        switch (stateOf(reference.mGate))
        {
            case Terrain::GateState::Open:
                return true;
            case Terrain::GateState::Closed:
            case Terrain::GateState::Unknown:
                return false;
            case Terrain::GateState::Undecided:
                break;
        }

        return !reference.mDisabled;
    }

    void CellPlacer::restand(Placement& placement, const bool shown)
    {
        if (!shown)
            return;

        const bool wanted = stands(placement);
        if (wanted && !placement.mStood.isStanding())
            stand(placement.mStood, mPlaced);
        else if (!wanted)
            drop(placement.mStood, mPlaced);
    }

    void CellPlacer::collectStanding(std::vector<ESM::RefNum>& into) const
    {
        for (const HeldCell& cell : mCells)
            for (std::size_t at = 0; at < cell.mShown; ++at)
                if (cell.mPlacements[at].mStood.isStanding())
                    into.push_back(cell.mPlacements[at].mState.mRefNum);
    }

    void CellPlacer::collectGateVerdicts(const osg::Vec4i& activeGrid, std::vector<GateVerdict>& into) const
    {
        for (const HeldCell& cell : mCells)
            if (inActiveGrid(cell.mCell, activeGrid))
                collectGateVerdicts(cell, into);
    }

    void CellPlacer::collectGateVerdicts(const HeldCell& cell, std::vector<GateVerdict>& into) const
    {
        const std::size_t first = into.size();
        const auto verdictOn = [&](const ReferenceState& reference) {
            if (reference.mGate == Terrain::sNoGate)
                return;

            const Terrain::GateState state = stateOf(reference.mGate);
            if (state == Terrain::GateState::Open || state == Terrain::GateState::Closed)
                into.push_back(
                    GateVerdict{ .mRefNum = reference.mRefNum, .mStands = state == Terrain::GateState::Open });
        };
        for (const Placement& placement : cell.mPlacements)
            verdictOn(placement.mState);
        for (const HeldLight& lamp : cell.mLights)
            verdictOn(lamp.mState);

        // A model of several parts is several placements of one reference, and a lamp is its
        // placements and its light, next to each other only by chance once the size rule's sort
        // has run.
        const auto begin = into.begin() + static_cast<std::ptrdiff_t>(first);
        std::sort(begin, into.end(),
            [](const GateVerdict& left, const GateVerdict& right) { return left.mRefNum < right.mRefNum; });
        into.erase(std::unique(begin, into.end(),
                       [](const GateVerdict& left, const GateVerdict& right) { return left.mRefNum == right.mRefNum; }),
            into.end());
    }

    Terrain::GateState CellPlacer::stateOf(const std::uint32_t gate) const
    {
        return gate < mGates.size() ? mGates[gate] : Terrain::GateState::Unknown;
    }

    bool CellPlacer::isListed(const std::vector<ESM::RefNum>& sorted, const ESM::RefNum refnum)
    {
        return !sorted.empty() && std::binary_search(sorted.begin(), sorted.end(), refnum);
    }

    bool CellPlacer::wantsFlattening(const osg::Vec2i& cell, const Material& ground, const WorldAround& around)
    {
        // A stack is flattened outside the active grid, where the quad tree flattens too, and a
        // single layer is never, because it is already a single fetch.
        return ground.mLayers.mCount > 1 && !inActiveGrid(cell, around.mActiveGrid);
    }

    HeldCell& CellPlacer::hold(const PreparedCell& cell, const WorldAround& around, ExtractionStats& stats)
    {
        // A spare comes back through `reuse`, so what it holds is room and nothing else.
        HeldCell held = mSpareCells.take();
        held.mCell = cell.mCell;
        held.mStatics = cell.mStatics;

        adoptGround(cell, held, around, stats);

        [[maybe_unused]] const auto [at, fresh] = mCells.insert(std::move(held));
        assert(fresh && "a cell adopted twice");
        return *at;
    }

    void CellPlacer::adoptGround(
        const PreparedCell& cell, HeldCell& held, const WorldAround& around, ExtractionStats& stats)
    {
        const PreparedGround& ground = cell.mGround;
        HeldGround& stands = held.mGround;
        assert(!stands.mStands && stands.mTextures.empty() && "a ground adopted over one still held");
        if (!ground.mStands)
            return;

        stands.mStands = true;
        mLayerScratch.clear();
        bool mapped = false;
        for (const PreparedLayer& layer : ground.mLayers)
        {
            MaterialLayer row = layer.mRow;

            // A table with no room left names the neutral texel: the device reads a layer's slot
            // with no test, as it reads a material's diffuse.
            const Index slot = mScene.textures().add(layer.mTexture->mPath, layer.mTexture->mImage.get());
            row.mDiffuse = slot != sNoIndex ? slot : Shaders::TEXTURE_NEUTRAL;

            // A normal map tiles with the diffuse; one the table has no room for is no normal map,
            // and the layer keeps the chunk's normal and its own coordinates.
            if (layer.mNormalTexture != nullptr)
            {
                row.mNormal = mScene.textures().add(layer.mNormalTexture->mPath, layer.mNormalTexture->mImage.get(),
                    TextureWrap::Repeat, TextureEncoding::Normal);
                if (layer.mParallax && row.mNormal != sNoIndex)
                    row.mFlags |= Shaders::LAYER_PARALLAX;
            }

            // What a `_diffusespec`'s alpha is, the layout says: a classic one is a highlight's
            // strength, which this renderer has no use for, and its colour is a diffuse like any.
            if (layer.mDiffuseSpec && mSpecularLayout == SpecularLayout::MetalRoughness)
                row.mFlags |= Shaders::LAYER_AUTHORED;

            mapped = mapped || row.mNormal != sNoIndex || row.mFlags != 0;

            // The row keeps no count: `maskOf` reads the grid's area back, so the run
            // the table hands out has to be exactly that long, which the reader asserts as it reads.
            if (!layer.mWeights.empty())
            {
                const Run mask = mScene.materials().addMask(layer.mWeights.in(std::span<const float>(ground.mWeights)));
                assert(mask.mCount == row.mMaskWidth * row.mMaskHeight && "a mask run that is not its grid's area");
                row.mMaskOffset = mask.mOffset;
            }

            mLayerScratch.push_back(row);
        }
        ground.collectTextures(stands.mTextures);

        stands.mStood.mTransform = osg::Matrixf::translate(ground.mOrigin);

        Material material;
        material.mKind = MaterialKind::Terrain;

        // Stated here, because the ground is nobody's node. Every other material reads its
        // mode off a state set `NifOsg` described, and this one is stood off the land records —
        // where `Terrain::ChunkManager` states the same thing for the rasterizer's chunks.
        material.mVertexColour = VertexColour::Tint;
        material.mLayersMapped = mapped;
        if (!mLayerScratch.empty())
            material.mLayers = mScene.materials().addLayers(mLayerScratch);
        material.mFlatten = wantsFlattening(held.mCell, material, around);
        stands.mStood.mMaterial = mScene.addMaterial(material);

        // A heightfield is neither a sheet nor closed, and no fold is needed to say so.
        stands.mStood.mMesh = mScene.addMesh(MeshArrays{ .mPositions = ground.mPositions,
                                                 .mNormals = ground.mNormals,
                                                 .mTexCoords = ground.mTexCoords,
                                                 .mColours = ground.mColours,
                                                 .mIndices = ground.mIndices },
            FoldedShape{});

        // Held on the scene, because no drawable and no state set will ever name them:
        // `dropGround` is what lets go.
        stands.mMeshHold = mScene.holdMesh(stands.mStood.mMesh);
        stands.mMaterialHold = mScene.holdMaterial(stands.mStood.mMaterial);

        ++stats.mMeshesAdded;
        ++stats.mMaterialsAdded;
    }

    void CellPlacer::adoptPlacements(const PreparedCell& cell, HeldCell& held, CellHolds& holds)
    {
        held.mPlacements.clear();
        held.mShown = 0;

        for (const PreparedRef& ref : cell.mRefs)
        {
            const PreparedModel& model = *cell.mModels[ref.mModel];
            const CellHolds::HeldModel& adopted = holds.knownOf(model);
            const ReferenceState state = heard(ref.mRefNum, ref.mGate);

            for (std::size_t at = 0; at < adopted.mParts.size(); ++at)
                held.mPlacements.push_back(Placement{
                    .mStood = {
                        .mMesh = adopted.mParts[at].mMesh,
                        .mMaterial = adopted.mParts[at].mMaterial,
                        .mTransform = model.mParts[at].mLocal * ref.mTransform,
                    },
                    .mRadius = ref.mRadius,
                    .mModes = model.mParts[at].mModes,
                    .mState = state,
                });
        }

        // Largest first, once, so the size rule's answer is a prefix on every walk after this.
        // Stable, so equal radii keep the order the references were read in and two runs of one
        // walk place the same slots.
        std::stable_sort(held.mPlacements.begin(), held.mPlacements.end(),
            [](const Placement& larger, const Placement& smaller) { return larger.mRadius > smaller.mRadius; });

        held.mLights.clear();
        for (const PreparedLight& lamp : cell.mLights)
            held.mLights.push_back(HeldLight{
                .mPosition = lamp.mPosition,
                .mRecord = lamp.mRecord,
                .mState = heard(lamp.mRefNum, lamp.mGate),
            });

        // By where each is met within a reference, so a change reaches a reference's placements in
        // slot order and then its lamps, as a walk of the rows would.
        held.mByReference.clear();
        held.mByReference.reserve(held.mPlacements.size() + held.mLights.size());
        for (std::size_t at = 0; at < held.mPlacements.size(); ++at)
            held.mByReference.push_back(ReferenceSpot{
                .mRefNum = held.mPlacements[at].mState.mRefNum,
                .mAt = static_cast<std::uint32_t>(at),
            });
        for (std::size_t at = 0; at < held.mLights.size(); ++at)
            held.mByReference.push_back(ReferenceSpot{
                .mRefNum = held.mLights[at].mState.mRefNum,
                .mAt = static_cast<std::uint32_t>(held.mPlacements.size() + at),
            });
        std::ranges::sort(held.mByReference, [](const ReferenceSpot& left, const ReferenceSpot& right) {
            return left.mRefNum < right.mRefNum || (left.mRefNum == right.mRefNum && left.mAt < right.mAt);
        });
    }

    ReferenceState CellPlacer::heard(const ESM::RefNum refnum, const std::uint32_t gate) const
    {
        return ReferenceState{
            .mRefNum = refnum,
            .mGate = gate,
            .mDisabled = isListed(mDisabled, refnum),
            .mBlacklisted = isListed(mBlacklisted, refnum),
        };
    }

    void CellPlacer::stand(Stood& stood, std::uint32_t& standing)
    {
        assert(!stood.isStanding() && "a thing stood twice");

        stood.mSlot = mScene.addInstance(MeshInstance{
            .mTransform = stood.mTransform,
            .mMesh = stood.mMesh,
            .mMaterial = stood.mMaterial,
            .mStander = Stander::Ring,
        });
        ++standing;
    }

    void CellPlacer::drop(Stood& stood, std::uint32_t& standing)
    {
        if (!stood.isStanding())
            return;

        mScene.dropInstance(stood.mSlot, Stander::Ring);
        stood.mSlot = sNoIndex;
        --standing;
    }

    void CellPlacer::dropGround(HeldCell& cell)
    {
        HeldGround& ground = cell.mGround;
        if (!ground.mStands)
            return;

        drop(ground.mStood, mGroundPlaced);

        // The ring's own holds, the last on the ground's rows once its placement went above.
        mScene.drop(std::move(ground.mMeshHold));
        mScene.drop(std::move(ground.mMaterialHold));
        ground.reuse();
    }

    void CellPlacer::dropSlots()
    {
        for (HeldCell& cell : mCells)
            dropSlots(cell);
    }

    void CellPlacer::dropSlots(HeldCell& cell)
    {
        for (std::size_t at = 0; at < cell.mShown; ++at)
            drop(cell.mPlacements[at].mStood, mPlaced);
        cell.mShown = 0;

        if (cell.mGround.mStands)
            drop(cell.mGround.mStood, mGroundPlaced);
    }

    bool CellPlacer::standsAsHeld(const WorldAround& around) const
    {
        for (const HeldCell& cell : mCells)
            if (!standsAsHeld(cell, around))
                return false;

        return standsNoMore();
    }

    bool CellPlacer::standsAsHeld(const HeldCell& cell, const WorldAround& around) const
    {
        const std::span<const PlacementRow> placed = mScene.placements().getRows();

        // Whether `stood` stands exactly where it says, or stands nowhere where `wanted` is false.
        const auto standsAs = [&](const Stood& stood, const bool wanted) {
            if (wanted != stood.isStanding())
                return false;
            if (!wanted)
                return true;
            if (stood.mSlot >= placed.size())
                return false;

            const MeshInstance& standing = placed[stood.mSlot].mInstance;
            return standing.isPlaced() && standing.mStander == Stander::Ring && standing.mMesh == stood.mMesh
                && standing.mMaterial == stood.mMaterial;
        };

        const bool inReach
            = around.mExterior && around.mWorld.mGrid.withinReach(cell.mCell, around.mEye, around.mReach);
        const bool shown = inReach && !inActiveGrid(cell.mCell, around.mActiveGrid);
        if (!shown && cell.mShown != 0)
            return false;

        for (std::size_t at = 0; at < cell.mPlacements.size(); ++at)
        {
            const Placement& placement = cell.mPlacements[at];
            if (!standsAs(placement.mStood, at < cell.mShown && stands(placement)))
                return false;
        }

        if (cell.mGround.mStands && !standsAs(cell.mGround.mStood, inReach))
            return false;

        return true;
    }

    bool CellPlacer::standsNoMore() const
    {
        std::uint32_t standing = 0;
        for (const PlacementRow& row : mScene.placements().getRows())
            if (row.mInstance.isPlaced() && row.mInstance.mStander == Stander::Ring)
                ++standing;

        return standing == getPlaced() + getGroundPlaced();
    }

    std::uint32_t CellPlacer::place(const WorldAround& around)
    {
        std::uint32_t lit = 0;
        for (HeldCell& cell : mCells)
            lit += place(cell, around);

        return lit;
    }

    std::uint32_t CellPlacer::place(HeldCell& cell, const WorldAround& around)
    {
        const bool inReach = around.mWorld.mGrid.withinReach(cell.mCell, around.mEye, around.mReach);

        // The ground stands inside the active grid too: the game builds none for this
        // renderer, so what a cell's land says is stood here wherever the cell is.
        if (cell.mGround.mStands)
        {
            HeldGround& ground = cell.mGround;

            if (inReach && !ground.mStood.isStanding())
                stand(ground.mStood, mGroundPlaced);
            else if (!inReach)
                drop(ground.mStood, mGroundPlaced);

            // A cell crossing the grid's edge shades the other way from now on. The composite it
            // held goes with the rewrite, and one it now wants is asked for by the row.
            const Material& stood = mScene.materials().getRows()[ground.mStood.mMaterial];
            if (const bool wanted = wantsFlattening(cell.mCell, stood, around); wanted != stood.mFlatten)
            {
                Material given = stood;
                given.mFlatten = wanted;
                given.mDiffuse = sNoIndex;
                given.mSpecular = sNoIndex;
                mScene.setMaterial(ground.mStood.mMaterial, given);
            }
        }

        const bool shown = inReach && !inActiveGrid(cell.mCell, around.mActiveGrid);

        // The paging's own rule: a reference is placed while its scaled radius clears the size
        // threshold at the eye's distance to its cell. The placements are sorted largest first, so
        // what clears is a prefix and where it ends is one search — and what this walk touches is
        // what entered or left that prefix since the last one, which on a standing frame is nothing.
        const float threshold
            = shown ? mMinSize * around.mWorld.mGrid.chebyshevDistanceTo(cell.mCell, around.mEye) : 0.0f;
        const float threshold2 = threshold * threshold;

        const std::span<Placement> placements = cell.mPlacements;
        const auto clears
            = [threshold2](const Placement& placement) { return placement.mRadius * placement.mRadius >= threshold2; };
        const std::size_t wanted = shown
            ? static_cast<std::size_t>(
                std::partition_point(placements.begin(), placements.end(), clears) - placements.begin())
            : 0;

        for (std::size_t at = cell.mShown; at < wanted; ++at)
            if (stands(placements[at]))
                stand(placements[at].mStood, mPlaced);
        for (std::size_t at = wanted; at < cell.mShown; ++at)
            drop(placements[at].mStood, mPlaced);
        cell.mShown = wanted;

        // On every walk rather than kept, because the walk empties the lights and a flame is a
        // function of the hour; what is kept is the record.
        if (!shown)
            return 0;

        std::uint32_t lit = 0;
        for (const HeldLight& lamp : cell.mLights)
        {
            if (!standsBy(lamp.mState))
                continue;

            const Result<std::optional<Light>, std::string_view> light = makeLight(
                lamp.mRecord, lamp.mPosition, around.mSimulationTime, static_cast<int>(lamp.mState.mRefNum.mIndex));
            assert(light.isOk() && "a lamp the reader carried that the frame refuses");
            if (!light.value().has_value())
                continue;

            mScene.addLight(*light.value());
            ++lit;
        }

        return lit;
    }
}
