#include "materialtable.hpp"

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

#include <components/crashcatcher/crash.hpp>
#include <components/misc/hashstate.hpp>
#include <components/rtx/image/textureencoding.hpp>

#include "texturetable.hpp"

namespace Rtx
{
    Index MaterialTable::add(TextureTable& textures, const Material& material)
    {
        holdTextures(textures, material);

        const Index index = takeRow(material);
        note(index);
        return index;
    }

    bool MaterialTable::set(TextureTable& textures, Index material, const Material& what)
    {
        Material& row = rowAt(material);
        if (row == what)
            return false;

        // A rewrite keeps the row's layer run, which is what the material's chunk was given and
        // what its composite is checked against; one that named other layers would leak the run
        // it left. Moving a material's layers is a drop and an add.
        assert(what.mLayers == row.mLayers && "a rewrite that moves a material's layers is a remove and an add");

        const bool reclassed = row.getTraversed() != what.getTraversed();

        // The new set taken before the old is given back. A flipbook that comes round to a frame
        // it already had names the same texture twice running; releasing first would take that slot
        // to zero, empty its path and hand it to the next thing that asked — a slot changing
        // identity under everything standing on it, on a frame where nothing was supposed to move.
        holdTextures(textures, what);
        dropTextures(textures, row);

        row = what;
        note(material);

        return reclassed;
    }

    void MaterialTable::note(Index slot)
    {
        mWritten.grow(size());
        mWritten.add(slot);
    }

    void MaterialTable::holdTextures(TextureTable& textures, const Material& material)
    {
        // The trace reads what a normal map's levels lost beside it (`normalMapSlopes`), which only
        // a slot taken as a normal map has: bound as any other data, it has no spread there, and the
        // companion a slot of no spread binds read as a loss is a gloss gone matt.
        Crash::contract(
            material.mNormal == sNoIndex || textures.getRows()[material.mNormal].mEncoding == TextureEncoding::Normal,
            "a material's normal map in a slot not taken as one");

        forEachTexture(material, [&](const Index texture) { textures.hold(texture); });
    }

    void MaterialTable::dropTextures(TextureTable& textures, const Material& material)
    {
        forEachTexture(material, [&](const Index texture) { textures.drop(texture); });
    }

    Run MaterialTable::addMask(std::span<const float> weights)
    {
        assert(!weights.empty() && "a mask of nothing is not a run");
        const Run run = mMaskRuns.allocate(static_cast<std::uint32_t>(weights.size()));
        mArrived.mMasks.push_back(
            ArrivedMask{ .mRun = run, .mStart = static_cast<std::uint32_t>(mArrived.mWeights.size()) });
        mArrived.mWeights.insert(mArrived.mWeights.end(), weights.begin(), weights.end());
        if (mKeepsDigests)
        {
            Misc::HashState digest;
            digest.add(weights);
            mMaskDigests.insert_or_assign(run.mOffset, digest.getWords());
        }
        ++mRunRevision;
        return run;
    }

    void MaterialTable::keepDigests()
    {
        assert(size() == 0 && mMaskRuns.getEnd() == 0 && "digests asked of a table that holds masks without one");
        mKeepsDigests = true;
    }

    const Misc::DigestWords& MaterialTable::getMaskDigest(const Run run) const
    {
        assert(mKeepsDigests && "a digest asked of a table that keeps none");
        const auto found = mMaskDigests.find(run.mOffset);
        assert(found != mMaskDigests.end() && "the digest of a mask that does not stand");
        return found->second;
    }

    Run MaterialTable::addLayers(std::span<const MaterialLayer> layers)
    {
        const Run run = mLayers.allocate(layers);
        mArrived.mLayers.push_back(run);
        ++mRunRevision;
        return run;
    }

    void MaterialTable::drop(TextureTable& textures, const Index material)
    {
        if (!dropRow(material))
            return;

        Material& going = rowAt(material);

        // What it named goes with it, and before its layer run does: the run is what says which
        // textures those were, and it is about to be handed to an allocator that will let the next
        // chunk write over it.
        dropTextures(textures, going);

        // Its layers and the masks behind them go with it. A material that carries layers is a
        // terrain chunk, so without this what accumulates is a blend map per chunk walked past; the
        // runs are variable length, which is why they are given back to an allocator rather than to
        // a list of slots.
        // A mask given back before its hand-over is staged by none: its room may be the next arrival's, which two
        // copies into one range would race for.
        // A layer that covers its chunk whole has no mask, a run of nothing at whatever offset it holds.
        for (const MaterialLayer& layer : going.mLayers.in(getLayers()))
        {
            const Run mask = maskOf(layer);
            if (mask.empty())
                continue;
            mMaskRuns.release(mask);
            std::erase_if(mArrived.mMasks, [&](const ArrivedMask& arrived) { return arrived.mRun == mask; });
            mMaskDigests.erase(mask.mOffset);
        }

        mLayers.release(going.mLayers);

        going = Material{};
        freeRow(material);
    }

    void MaterialTable::clearArrivals()
    {
        mWritten.clear();
        mArrived.clear();
    }
}
