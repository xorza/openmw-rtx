#include "cellplacer.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <osg/Matrixf>

#include <components/misc/result.hpp>
#include <components/rtx/common/rowspan.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/image/alpharange.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/mirror/lightbuilder.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/placementtable.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/surface.hpp>
#include <components/rtx/scene/textureasks.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/terrain/pagedcellref.hpp>

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

    void CellPlacer::moveReference(const ESM::RefNum refnum, const ReferencePlace& place, const ReferenceStands stands)
    {
        const bool away = stands == ReferenceStands::Away;
        auto at = std::lower_bound(mMoved.begin(), mMoved.end(), refnum,
            [](const Moved& known, ESM::RefNum asked) { return known.mRefNum < asked; });
        const bool known = at != mMoved.end() && at->mRefNum == refnum;
        if (!known)
            at = mMoved.insert(at, Moved{ .mRefNum = refnum, .mTransform = {}, .mAway = away, .mHome = std::nullopt });
        at->mTransform = transformOf(place);
        at->mAway = away;

        // **The one cell that holds it, once found**: a script swings a lantern on every frame its
        // cell is loaded, and a search of every cell held for each swing was a search of hundreds.
        if (at->mHome.has_value())
        {
            const auto home = mCells.find(*at->mHome);
            if (home != mCells.end())
                standAt(*home, refnum, &at->mTransform, away);
            return;
        }

        // **And no cell, once searched**: a reference no held cell holds — an actor the scene
        // graph mirrors, which the physics moves on every frame — was searched for in every cell
        // held on each move. A cell held after the search looks for it as it is held (`adoptPlacements`).
        if (known)
            return;
        for (HeldCell& held : mCells)
            if (standAt(held, refnum, &at->mTransform, away))
                at->mHome = held.mCell;
    }

    void CellPlacer::setGate(const std::uint32_t gate, const Terrain::GateState state)
    {
        assert(gate != Terrain::sNoGate && "a gate told of that is none");
        if (gate >= mGates.size())
            mGates.resize(gate + 1, Terrain::GateState::Unknown);
        mGates[gate] = state;

        for (HeldCell& cell : mCells)
        {
            const auto [first, last] = std::ranges::equal_range(cell.mByGate, gate, std::less<>{}, &GateSpot::mGate);
            for (const GateSpot& spot : std::ranges::subrange(first, last))
                if (spot.mAt < cell.mPlacements.size())
                    restand(cell.mPlacements[spot.mAt], spot.mAt < cell.mShown, mPlaced);
            // Nothing to restand of a lamp: `place` builds every lamp's light again on every walk, and reads the gate
            // then.
        }
    }

    void CellPlacer::setNightDay(const NightDayMode mode)
    {
        if (mode == mNightDay)
            return;
        mNightDay = mode;

        // One frame restands every switched placement held, as the game turns every switch of its
        // own cells on that frame.
        forEachPlacementWhere([](const Placement& placement) { return !placement.mModes.isEvery(); },
            [&](Placement& placement, bool shown) { restand(placement, shown, mPlaced); });
        for (HeldGrass& grass : mGrass)
            for (Placement& placement : grass.mPlacements)
                if (!placement.mModes.isEvery())
                    restand(placement, grass.mShown, mGrassPlaced);
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
                restand(placement, shown, mPlaced);
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
                restand(placement, spot.mAt < cell.mShown, mPlaced);
            }
        }
    }

    void CellPlacer::forgetReferences()
    {
        for (const Moved& moved : mMoved)
            if (moved.mHome.has_value())
                if (const auto home = mCells.find(*moved.mHome); home != mCells.end())
                    standAt(*home, moved.mRefNum, nullptr, false);
        mMoved.clear();

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
        if (reference.mBlacklisted || reference.mAway)
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

    void CellPlacer::restand(Placement& placement, const bool shown, std::uint32_t& standing)
    {
        if (!shown)
            return;

        const bool wanted = stands(placement);
        if (wanted && !placement.mStood.isStanding())
            stand(placement.mStood, standing);
        else if (!wanted)
            drop(placement.mStood, standing);
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

    GroundFlattening CellPlacer::flatteningOf(const osg::Vec2i& cell, const Material& ground, const WorldAround& around)
    {
        if (ground.mLayers.mCount <= 1)
            return GroundFlattening::None;

        return inActiveGrid(cell, around.mActiveGrid) ? GroundFlattening::Undetailed : GroundFlattening::Every;
    }

    void CellPlacer::holdCell(
        const PreparedCell& cell, const WorldAround& around, ExtractionStats& stats, CellHolds& holds)
    {
        // A spare comes back through `reuse`, so what it holds is room and nothing else.
        HeldCell held = mSpareCells.take();
        held.mCell = cell.mCell;

        adoptGround(cell, held, around, stats);
        held.mModels.assign(cell.mModels.begin(), cell.mModels.end());
        adoptPlacements(cell, held, holds);
        gatherAsks(held);

        [[maybe_unused]] const auto [at, fresh] = mCells.insert(std::move(held));
        assert(fresh && "a cell adopted twice");
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

        // The heights every layer with one spans together, which bound the march through the stack's summed
        // relief: the material's relief, as a surface's is its normal map's. The material's and not each
        // layer's, since the march reads the stack's sum and nothing reads a layer's alone.
        std::optional<AlphaRange> relief;
        for (const PreparedLayer& layer : ground.mLayers)
        {
            MaterialLayer row = layer.mRow;

            // A table with no room left names the neutral texel: the device reads a layer's slot
            // with no test, as it reads a material's diffuse.
            const TextureIndex slot = mScene.textures().add(
                layer.mTexture->mPath, layer.mTexture->mImage.get(), TextureWrap::Repeat, TextureEncoding::Colour);
            row.mDiffuse = slot != sNoRow ? slot.at() : Shaders::TEXTURE_NEUTRAL;

            // A normal map tiles with the diffuse; one the table has no room for is no normal map,
            // and the layer keeps the chunk's normal and its own coordinates.
            if (layer.mNormalTexture != nullptr)
            {
                row.mNormal = mScene.textures()
                                  .add(layer.mNormalTexture->mPath, layer.mNormalTexture->mImage.get(),
                                      TextureWrap::Repeat, TextureEncoding::Normal)
                                  .at();
                if (layer.mRelief.has_value() && row.mNormal != Shaders::NO_TEXTURE)
                {
                    row.mFlags |= Shaders::LAYER_PARALLAX;
                    relief = relief.has_value() ? AlphaRange{ .mLow = std::min(relief->mLow, layer.mRelief->mLow),
                        .mHigh = std::max(relief->mHigh, layer.mRelief->mHigh) }
                                                : *layer.mRelief;
                }
            }

            // A `_diffusespec` is an authored albedo and its roughness (`Shaders::LAYER_AUTHORED`).
            if (layer.mDiffuseSpec)
                row.mFlags |= Shaders::LAYER_AUTHORED;

            mapped = mapped || row.mNormal != Shaders::NO_TEXTURE || row.mFlags != 0;

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
        material.mRelief = relief;
        if (!mLayerScratch.empty())
            material.mLayers = mScene.materials().addLayers(mLayerScratch);
        material.mFlatten = flatteningOf(held.mCell, material, around);
        stands.mStood.mMaterial = mScene.addMaterial(material);

        // A heightfield is neither a sheet nor closed, and no fold is needed to say so.
        stands.mStood.mMesh = mScene.addMesh(ground.mesh());

        // Held on the scene, because no drawable and no state set will ever name them:
        // `dropGround` is what lets go.
        stands.mMeshHold = mScene.holdMesh(stands.mStood.mMesh, HoldTier::Kept);
        stands.mMaterialHold = mScene.holdMaterial(stands.mStood.mMaterial, HoldTier::Kept);

        ++stats.mMeshesAdded;
        ++stats.mMaterialsAdded;
    }

    void CellPlacer::appendPlacements(const PreparedModel& model, const CellHolds::HeldModel& adopted,
        const PreparedRef& ref, const std::uint32_t reference, const ReferenceState& state,
        std::vector<Placement>& into)
    {
        for (std::size_t at = 0; at < adopted.mParts.size(); ++at)
            into.push_back(Placement{
                .mStood = {
                    .mMesh = adopted.mParts[at].mMesh,
                    .mMaterial = adopted.mParts[at].mMaterial,
                    .mTransform = model.mParts[at].mLocal * ref.mTransform,
                    .mLampBody = ref.mLampBody,
                    .mClockwise = model.mParts[at].mClockwise,
                },
                .mRadius = ref.mRadius,
                .mModes = model.mParts[at].mModes,
                .mDrawable = model.mParts[at].mDrawable.get(),
                .mLocal = &model.mParts[at].mLocal,
                .mReference = reference,
                .mState = state,
            });
    }

    void CellPlacer::retierGround(const osg::Vec2i& cell, const HoldTier tier)
    {
        const auto held = mCells.find(cell);
        assert(held != mCells.end() && "a ground retiered whose cell is not held");
        retierGround(held->mGround, tier);
    }

    HoldTier CellPlacer::getGroundTier(const osg::Vec2i& cell) const
    {
        const auto held = mCells.find(cell);
        return held == mCells.end() || !held->mGround.mStands ? HoldTier::Unheld : held->mGround.mMeshHold.getTier();
    }

    void CellPlacer::retierGround(HeldGround& ground, const HoldTier tier)
    {
        if (!ground.mStands || ground.mMeshHold.getTier() == tier)
            return;
        assert(ground.mMaterialHold.getTier() == ground.mMeshHold.getTier() && "a ground held at two tiers");

        MeshHold mesh = mScene.holdMesh(ground.mStood.mMesh, tier);
        MaterialHold material = mScene.holdMaterial(ground.mStood.mMaterial, tier);
        mScene.drop(std::move(ground.mMeshHold));
        mScene.drop(std::move(ground.mMaterialHold));
        ground.mMeshHold = std::move(mesh);
        ground.mMaterialHold = std::move(material);
    }

    void CellPlacer::adoptPlacements(const PreparedCell& cell, HeldCell& held, CellHolds& holds)
    {
        held.mPlacements.clear();
        held.mPlaces.clear();
        held.mShown = 0;

        for (const PreparedRef& ref : cell.mRefs)
        {
            const PreparedModel& model = *cell.mModels[ref.mModel];
            appendPlacements(model, holds.knownOf(model), ref, static_cast<std::uint32_t>(held.mPlaces.size()),
                heard(ref.mRefNum, ref.mGate), held.mPlacements);
            held.mPlaces.push_back(ref.mTransform);
        }

        // Largest first, as the reader sorted the references, so the size rule's answer is a prefix on every walk.
        assert(std::ranges::is_sorted(held.mPlacements, std::ranges::greater{}, &Placement::mRadius)
            && "a cell's references read out of the size rule's order");

        held.mLights.clear();
        for (const PreparedLight& lamp : cell.mLights)
            held.mLights.push_back(HeldLight{
                .mPosition = lamp.mAnchor * lamp.mTransform,
                .mAnchor = lamp.mAnchor,
                .mContent = lamp.mTransform,
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

        held.mByGate.clear();
        for (std::size_t at = 0; at < held.mPlacements.size(); ++at)
            if (const std::uint32_t gate = held.mPlacements[at].mState.mGate; gate != Terrain::sNoGate)
                held.mByGate.push_back(GateSpot{ .mGate = gate, .mAt = static_cast<std::uint32_t>(at) });
        for (std::size_t at = 0; at < held.mLights.size(); ++at)
            if (const std::uint32_t gate = held.mLights[at].mState.mGate; gate != Terrain::sNoGate)
                held.mByGate.push_back(
                    GateSpot{ .mGate = gate, .mAt = static_cast<std::uint32_t>(held.mPlacements.size() + at) });
        std::ranges::sort(held.mByGate, [](const GateSpot& left, const GateSpot& right) {
            return left.mGate < right.mGate || (left.mGate == right.mGate && left.mAt < right.mAt);
        });

        // Where the game moved one before this cell was held. Nothing stands yet, so this only
        // places it.
        for (Moved& moved : mMoved)
            if (standAt(held, moved.mRefNum, &moved.mTransform, moved.mAway))
                moved.mHome = held.mCell;
    }

    bool CellPlacer::standAt(
        HeldCell& cell, const ESM::RefNum refnum, const osg::Matrixf* const transform, const bool away)
    {
        const auto [first, last]
            = std::ranges::equal_range(cell.mByReference, refnum, std::less<>{}, &ReferenceSpot::mRefNum);
        for (const ReferenceSpot& spot : std::ranges::subrange(first, last))
        {
            if (spot.mAt >= cell.mPlacements.size())
            {
                HeldLight& lamp = cell.mLights[spot.mAt - cell.mPlacements.size()];
                lamp.mPosition = lamp.mAnchor * (transform != nullptr ? *transform : lamp.mContent);
                lamp.mState.mAway = away;
                continue;
            }

            // Out of the top level and in again, because an instance stands where it was added.
            Placement& placement = cell.mPlacements[spot.mAt];
            drop(placement.mStood, mPlaced);
            placement.mStood.mTransform
                = *placement.mLocal * (transform != nullptr ? *transform : cell.mPlaces[placement.mReference]);
            placement.mState.mAway = away;
            restand(placement, spot.mAt < cell.mShown, mPlaced);
        }
        return first != last;
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
            .mClockwise = stood.mClockwise,
            .mLampBody = stood.mLampBody,
            .mStander = Stander::Ring,
        });
        ++standing;
    }

    void CellPlacer::drop(Stood& stood, std::uint32_t& standing)
    {
        if (!stood.isStanding())
            return;

        mScene.dropInstance(stood.mSlot, Stander::Ring);
        stood.mSlot = sNoRow;
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

    void CellPlacer::holdGrass(const PreparedGrass& grass, CellHolds& holds)
    {
        HeldGrass held = mSpareGrass.take();
        held.mCell = grass.mCell;

        for (const PreparedRef& ref : grass.mRefs)
        {
            // A grass reference is no lamp and has no radius: the reader reads it with neither. No
            // script names one, so the game never moves one and none is named by its place.
            const PreparedModel& model = *grass.mModels[ref.mModel];
            appendPlacements(
                model, holds.knownOf(model), ref, 0, ReferenceState{ .mRefNum = ref.mRefNum }, held.mPlacements);
        }
        held.mModels.assign(grass.mModels.begin(), grass.mModels.end());

        // Each material once, at the finest footprint, for as long as the cell's grass is held.
        for (const Placement& placement : held.mPlacements)
            if (placement.mStood.mMaterial != sNoRow)
                held.mAsked.push_back(placement.mStood.mMaterial);
        std::ranges::sort(held.mAsked);
        held.mAsked.erase(std::ranges::unique(held.mAsked).begin(), held.mAsked.end());
        for (const MaterialIndex material : held.mAsked)
            mScene.askMaterial(material, 0);

        [[maybe_unused]] const auto [at, fresh] = mGrass.insert(std::move(held));
        assert(fresh && "a cell's grass adopted twice");
    }

    void CellPlacer::gatherAsks(HeldCell& held)
    {
        const auto meshes = mScene.meshes().getRows();
        held.mAsks.clear();

        // `0.5 log2(density / scale²)`, the density of the sets the mesh brings at their least: a mesh with no second
        // set reads every unit through its first (`MeshArrays::mUnitStreams`).
        const auto ask = [&](const Stood& stood) {
            if (stood.mMaterial == sNoRow || stood.mMesh == sNoRow)
                return;
            const MeshRange& mesh = meshes[stood.mMesh];
            const float density = mesh.mSecondTexCoords.mCount > 0
                ? std::min(mesh.mUvDensity.mFirst, mesh.mUvDensity.mSecond)
                : mesh.mUvDensity.mFirst;
            held.mAsks.push_back(RingAsk{ .mMaterial = stood.mMaterial,
                .mFootprint = 0.5f * std::log2(density) - std::log2(placedScale(stood.mTransform)) });
        };
        for (const Placement& placement : held.mPlacements)
            ask(placement.mStood);
        if (held.mGround.mStands)
            ask(held.mGround.mStood);

        // One a material, at its finest.
        std::ranges::sort(held.mAsks, [](const RingAsk& left, const RingAsk& right) {
            return left.mMaterial != right.mMaterial ? left.mMaterial < right.mMaterial
                                                     : left.mFootprint < right.mFootprint;
        });
        held.mAsks.erase(std::ranges::unique(held.mAsks, {}, &RingAsk::mMaterial).begin(), held.mAsks.end());

        // Largest first, so the first is how far any placement reaches past the square.
        held.mReachOut = held.mPlacements.empty() ? 0.0f : held.mPlacements.front().mRadius;
    }

    void CellPlacer::askByDistance(
        HeldCell& cell, const WorldAround& around, const std::span<const osg::Vec3f> predicted, const bool sightMoved)
    {
        const CellGrid& grid = around.mWorld.mGrid;
        const float size = grid.getCellSize();
        float nearest = grid.distanceSquaredTo(cell.mCell, around.mEye);
        for (const osg::Vec3f& centre : predicted)
            nearest = std::min(nearest, grid.distanceSquaredTo(cell.mCell, centre));
        const float distance = std::max(0.0f, std::sqrt(nearest) - cell.mReachOut);
        const float asked = distance < size ? 0.0f : size * std::exp2(std::floor(std::log2(distance / size)) - 1.0f);
        if (!sightMoved && cell.mAskedAt == asked)
            return;
        cell.mAskedAt = asked;

        // The cone's width at the distance asked, as a level: minus infinity at no distance or no spread, which asks
        // the finest. A footprint of plus infinity, a mesh of no triangles, against it is not a number, the finest too.
        const float cone = std::log2(asked * mSight.mSpreadAngle) + mSight.mLevelBias;
        for (RingAsk& ask : cell.mAsks)
        {
            const auto step = static_cast<std::uint8_t>(footprintStep(ask.mFootprint + cone));
            if (ask.mStep == step)
                continue;
            mScene.askMaterial(ask.mMaterial, step);
            if (ask.mStep.has_value())
                mScene.unaskMaterial(ask.mMaterial, *ask.mStep);
            ask.mStep = step;
        }
    }

    void CellPlacer::unaskAll(HeldCell& cell)
    {
        for (RingAsk& ask : cell.mAsks)
            if (ask.mStep.has_value())
            {
                mScene.unaskMaterial(ask.mMaterial, *ask.mStep);
                ask.mStep.reset();
            }
        cell.mAskedAt.reset();
    }

    void CellPlacer::unaskAll(HeldGrass& grass)
    {
        for (const MaterialIndex material : grass.mAsked)
            mScene.unaskMaterial(material, 0);
        grass.mAsked.clear();
    }

    void CellPlacer::dropSlots()
    {
        for (HeldCell& cell : mCells)
            dropSlots(cell);
        for (HeldGrass& grass : mGrass)
            dropSlots(grass);
    }

    void CellPlacer::dropSlots(HeldGrass& grass)
    {
        for (Placement& placement : grass.mPlacements)
            drop(placement.mStood, mGrassPlaced);
        grass.mShown = false;
    }

    bool CellPlacer::showsGrass(const osg::Vec2i& cell, const WorldAround& around)
    {
        return around.mExterior && around.mGroundcoverReach > 0.0f
            && around.mWorld.mGrid.withinReach(cell, around.mEye, around.mGroundcoverReach);
    }

    void CellPlacer::dropSlots(HeldCell& cell)
    {
        for (std::size_t at = 0; at < cell.mShown; ++at)
            drop(cell.mPlacements[at].mStood, mPlaced);
        cell.mShown = 0;

        if (cell.mGround.mStands)
            drop(cell.mGround.mStood, mGroundPlaced);
    }

    RingStanding CellPlacer::countStanding(const osg::Vec2i& eye) const
    {
        // Each mesh at the nearest cell holding it, and standing where any placement of it stands anywhere.
        constexpr std::uint8_t sNowhere = std::numeric_limits<std::uint8_t>::max();
        static_assert(sRingDistances < sNowhere, "a distance told from a mesh no cell holds");
        const auto meshes = mScene.meshes().getRows();
        std::vector<std::uint8_t> nearest(meshes.size(), sNowhere);
        std::vector<bool> standing(meshes.size(), false);

        RingStanding counted;
        for (const HeldCell& cell : mCells)
        {
            const std::int32_t away = std::max(std::abs(cell.mCell.x() - eye.x()), std::abs(cell.mCell.y() - eye.y()));
            const auto distance
                = static_cast<std::uint8_t>(std::min(static_cast<std::size_t>(away), sRingDistances - 1));
            RingDistance& at = counted.mByDistance[distance];
            for (const Placement& placement : cell.mPlacements)
            {
                const bool stood = placement.mStood.isStanding();
                ++at.mPlacements;
                at.mPlacementsStanding += stood ? 1 : 0;
                const MeshIndex mesh = placement.mStood.mMesh;
                assert(meshes.holds(mesh) && "a placement of a mesh the scene does not hold");
                nearest[mesh.at()] = std::min(nearest[mesh.at()], distance);
                standing[mesh.at()] = standing[mesh.at()] || stood;
            }
        }

        for (const MeshIndex mesh : everyRow<MeshIndex>(meshes.size()))
        {
            if (nearest[mesh.at()] == sNowhere)
                continue;
            RingDistance& at = counted.mByDistance[nearest[mesh.at()]];
            const std::uint32_t vertices = meshes[mesh].mVertices.mCount;
            const bool stood = standing[mesh.at()];
            ++at.mMeshes;
            at.mVertices += vertices;
            at.mMeshesStanding += stood ? 1 : 0;
            at.mVerticesStanding += stood ? vertices : 0;
        }
        return counted;
    }

    bool CellPlacer::standsAsHeld(const WorldAround& around) const
    {
        for (const HeldCell& cell : mCells)
            if (!standsAsHeld(cell, around))
                return false;
        for (const HeldGrass& grass : mGrass)
            if (!standsAsHeld(grass, around))
                return false;

        return standsNoMore();
    }

    bool CellPlacer::standsAs(const Stood& stood, const bool wanted) const
    {
        if (wanted != stood.isStanding())
            return false;
        if (!wanted)
            return true;

        const auto placed = mScene.placements().getRows();
        if (!placed.holds(stood.mSlot))
            return false;

        const MeshInstance& standing = placed[stood.mSlot].mInstance;
        return mScene.placements().isPlaced(stood.mSlot) && standing.mStander == Stander::Ring
            && standing.mMesh == stood.mMesh && standing.mMaterial == stood.mMaterial;
    }

    bool CellPlacer::standsAsHeld(const HeldGrass& grass, const WorldAround& around) const
    {
        const bool shown = showsGrass(grass.mCell, around);
        if (shown != grass.mShown)
            return false;

        return std::all_of(grass.mPlacements.begin(), grass.mPlacements.end(),
            [&](const Placement& placement) { return standsAs(placement.mStood, shown && stands(placement)); });
    }

    bool CellPlacer::standsAsHeld(const HeldCell& cell, const WorldAround& around) const
    {
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

    std::span<const Placement> CellPlacer::placementsIn(const osg::Vec2i& cell) const
    {
        const auto found = mCells.find(cell);
        if (found == mCells.end())
            return {};
        return found->mPlacements;
    }

    bool CellPlacer::standsGround(const osg::Vec2i& cell) const
    {
        const auto found = mCells.find(cell);
        return found != mCells.end() && found->mGround.mStood.isStanding();
    }

    bool CellPlacer::standsNoMore() const
    {
        std::uint32_t standing = 0;
        mScene.placements().forEachPlaced([&](PlacementIndex, const PlacementRow& row) {
            standing += row.mInstance.mStander == Stander::Ring ? 1u : 0u;
        });

        return standing == getPlaced() + getGroundPlaced() + getGrassPlaced();
    }

    std::uint32_t CellPlacer::place(const WorldAround& around, const std::span<const osg::Vec3f> predicted)
    {
        const bool sightMoved = around.mSight != mSight;
        mSight = around.mSight;

        std::uint32_t lit = 0;
        for (HeldCell& cell : mCells)
        {
            askByDistance(cell, around, predicted, sightMoved);
            lit += place(cell, around);
        }

        // What a walk touches is a cell's grass crossing the reach, and nothing on a standing frame.
        for (HeldGrass& grass : mGrass)
        {
            const bool shown = showsGrass(grass.mCell, around);
            if (shown == grass.mShown)
                continue;

            if (!shown)
            {
                dropSlots(grass);
                continue;
            }

            for (Placement& placement : grass.mPlacements)
                restand(placement, true, mGrassPlaced);
            grass.mShown = true;
        }

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

            // A cell crossing the grid's edge shades the other way from now on. **The composite it
            // holds stays**: it is the stack's sum on either side of the edge, and only which hits
            // read it changes.
            const Material& stood = mScene.materials().getRows()[ground.mStood.mMaterial];
            if (const GroundFlattening wanted = flatteningOf(cell.mCell, stood, around); wanted != stood.mFlatten)
            {
                Material given = stood;
                given.mFlatten = wanted;
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

            const Result<std::optional<Light>, std::string_view> light = makeLight(lamp.mRecord, lamp.mPosition,
                around.mSimulationTime, static_cast<std::int32_t>(lamp.mState.mRefNum.mIndex));
            assert(light.isOk() && "a lamp the reader carried that the frame refuses");
            if (!light.value().has_value())
                continue;

            mScene.addLight(*light.value());
            ++lit;
        }

        return lit;
    }
}
