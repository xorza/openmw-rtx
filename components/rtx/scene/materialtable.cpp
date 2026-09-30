#include "materialtable.hpp"

#include <cassert>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/image/textureencoding.hpp>

#include "texturetable.hpp"

namespace Rtx
{
    Index MaterialTable::add(TextureTable& textures, const Material& material)
    {
        holdTextures(textures, material);

        const Index index = mRows.take(material);
        note(index);
        return index;
    }

    bool MaterialTable::set(TextureTable& textures, Index material, const Material& what)
    {
        Material& row = mRows.at(material);
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
        mWritten.grow(mRows.size());
        mWritten.add(slot);
    }

    void MaterialTable::holdTextures(TextureTable& textures, const Material& material)
    {
        // The trace reads what a normal map's levels lost beside it (`normalMapSlopes`), which only
        // a slot taken as a normal map has: bound as any other data, it has the neutral shading map
        // there, and that read as a loss is a gloss gone matt.
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
        const Run run = mMasks.allocate(weights);
        mArrived.mMasks.push_back(run);
        ++mRunRevision;
        return run;
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
        if (!mRows.drop(material))
            return;

        Material& going = mRows.at(material);

        // What it named goes with it, and before its layer run does: the run is what says which
        // textures those were, and it is about to be handed to an allocator that will let the next
        // chunk write over it.
        dropTextures(textures, going);

        // Its layers and the masks behind them go with it. A material that carries layers is a
        // terrain chunk, so without this what accumulates is a blend map per chunk walked past; the
        // runs are variable length, which is why they are given back to an allocator rather than to
        // a list of slots.
        for (const MaterialLayer& layer : going.mLayers.in(getLayers()))
            mMasks.release(maskOf(layer));

        mLayers.release(going.mLayers);

        going = Material{};
        mRows.free(material);
    }

    void MaterialTable::clearArrivals()
    {
        mWritten.clear();
        mArrived.clear();
    }
}
