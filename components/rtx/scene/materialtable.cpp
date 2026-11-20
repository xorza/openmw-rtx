#include "materialtable.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <components/crashcatcher/crash.hpp>
#include <components/misc/hashstate.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/shaders/scene.h>

#include "texturetable.hpp"

namespace Rtx
{
    namespace
    {
        /// `0.5 log2(T w h)` for a texture of `size` texels read through a transform that lays `scale` of its
        /// coordinates on each of the mesh's, `T` the area that scale stretches by: minus infinity where the size is
        /// unknown, which asks level 0 of it.
        float offsetOf(const osg::Image* const image, const float scale)
        {
            if (image == nullptr || image->s() <= 0 || image->t() <= 0)
                return -std::numeric_limits<float>::infinity();
            return 0.5f * std::log2(scale * static_cast<float>(image->s()) * static_cast<float>(image->t()));
        }

        /// How much area a texture transform `uv * xy + zw` stretches the mesh's coordinates by, held to one at the
        /// least: what the walk of a material's maps uses for every map, as only some read through the transform,
        /// and a stretch of under one would read coarser.
        float stretchOf(const osg::Vec4f& transform)
        {
            return std::max(std::abs(transform.x() * transform.y()), 1.0f);
        }
    }

    bool MaterialTable::readsAlike(const Material& one, const Material& other)
    {
        // The layers are not compared: a rewrite keeps the run, which `set` asserts.
        return one.mDiffuse == other.mDiffuse && one.mEmissive == other.mEmissive && one.mDark == other.mDark
            && one.mNormal == other.mNormal && one.mSpecular == other.mSpecular && one.mDetail == other.mDetail
            && one.mDecal == other.mDecal && one.mBump == other.mBump && one.mGloss == other.mGloss
            && one.mEnvironment == other.mEnvironment
            && stretchOf(one.mTextureTransform) == stretchOf(other.mTextureTransform);
    }

    MaterialIndex MaterialTable::add(TextureTable& textures, const Material& material)
    {
        const MaterialIndex index = takeRow(material);
        holdTextures(textures, material, textureTierOf(index));
        mAsks.growTo(index);
        assert(mAsks[index].empty() && "a material slot taken over with its asks still standing");
        note(index);
        return index;
    }

    template <class Visit>
    void MaterialTable::forEachRead(const TextureTable& textures, const Material& material, Visit visit) const
    {
        constexpr float none = -std::numeric_limits<float>::infinity();
        const RowSpan<const TextureRow, TextureIndex> rows = textures.getRows();
        const auto imageOf = [&](const TextureIndex slot) -> const osg::Image* {
            return rows.holds(slot) ? rows[slot].mImage.get() : nullptr;
        };

        const float stretch = stretchOf(material.mTextureTransform);
        for (const TextureIndex map : { material.mDiffuse, material.mEmissive, material.mDark, material.mNormal,
                 material.mSpecular, material.mDetail, material.mDecal, material.mBump, material.mGloss })
            if (map != sNoRow)
                visit(map, offsetOf(imageOf(map), stretch), sTextureLevels - 1);

        if (material.mEnvironment != sNoRow)
            visit(material.mEnvironment, none, sTextureLevels - 1);

        for (const MaterialLayer& layer : material.mLayers.in(getLayers()))
        {
            const float scale = std::abs(layer.mDiffuseTransform.x() * layer.mDiffuseTransform.y());
            for (const std::uint32_t at :
                { static_cast<std::uint32_t>(layer.mDiffuse), static_cast<std::uint32_t>(layer.mNormal) })
            {
                if (at >= TextureTable::sCapacity)
                    continue;
                const TextureIndex map(at);

                // The bake's level, `groundLod`: the layer's texels across the chunk along its wider axis against
                // the composite's, rounded down.
                const osg::Image* const image = imageOf(map);
                std::size_t ceiling = 0;
                if (image != nullptr && image->s() > 0 && image->t() > 0)
                {
                    const float across
                        = std::max({ static_cast<float>(image->s()) * std::abs(layer.mDiffuseTransform.x()),
                            static_cast<float>(image->t()) * std::abs(layer.mDiffuseTransform.y()),
                            static_cast<float>(Shaders::GROUND_COMPOSITE_EXTENT) });
                    const float level
                        = std::floor(std::log2(across / static_cast<float>(Shaders::GROUND_COMPOSITE_EXTENT)));
                    ceiling = level > 0.0f ? std::min(static_cast<std::size_t>(level), sTextureLevels - 1) : 0;
                }
                visit(map, offsetOf(image, scale), ceiling);
            }
        }
    }

    void MaterialTable::askTextures(TextureTable& textures, const Material& material,
        const std::optional<std::uint8_t> before, const std::optional<std::uint8_t> after) const
    {
        if (before == after)
            return;

        forEachRead(textures, material, [&](const TextureIndex slot, const float offset, const std::size_t ceiling) {
            const std::optional<std::size_t> was
                = before.has_value() ? std::optional(levelAsked(*before, offset, ceiling)) : std::nullopt;
            const std::optional<std::size_t> now
                = after.has_value() ? std::optional(levelAsked(*after, offset, ceiling)) : std::nullopt;
            if (was == now)
                return;
            if (now.has_value())
                textures.ask(slot, *now);
            if (was.has_value())
                textures.unask(slot, *was);
        });
    }

    void MaterialTable::ask(TextureTable& textures, const MaterialIndex material, const std::size_t step)
    {
        assert(isLive(material) && "an ask of a material nothing holds");
        FootprintAsks& asks = mAsks[material];
        const std::optional<std::uint8_t> before = asks.finest();
        asks.ask(step);
        askTextures(textures, rowAt(material), before, asks.finest());
    }

    void MaterialTable::unask(TextureTable& textures, const MaterialIndex material, const std::size_t step)
    {
        assert(isLive(material) && "an ask taken back of a material nothing holds");
        FootprintAsks& asks = mAsks[material];
        const std::optional<std::uint8_t> before = asks.finest();
        asks.unask(step);
        askTextures(textures, rowAt(material), before, asks.finest());
    }

    bool MaterialTable::set(TextureTable& textures, MaterialIndex material, const Material& what)
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
        const HoldTier tier = textureTierOf(material);
        holdTextures(textures, what, tier);

        // What it asks of its textures moves with what it names: the new set asked before the old is taken back,
        // as it is held before the old is given back, and the old taken back before it is given back, since a slot
        // freed by the drop must stand no ask.
        // A rewrite that reads the same slots at the same stretch moves no ask: a scrolling transform, lava and
        // waterfalls, rewrites hundreds of materials a frame and changes only where they are read.
        const std::optional<std::uint8_t> finest = mAsks[material].finest();
        if (!readsAlike(row, what))
        {
            askTextures(textures, what, std::nullopt, finest);
            askTextures(textures, row, finest, std::nullopt);
        }

        dropTextures(textures, row, tier);

        row = what;
        note(material);

        return reclassed;
    }

    void MaterialTable::note(MaterialIndex slot)
    {
        mWritten.grow(size());
        mWritten.add(slot);
    }

    HoldTier MaterialTable::textureTierOf(const MaterialIndex material) const
    {
        const HoldTier tier = getTier(material);
        return tier == HoldTier::Unheld ? HoldTier::Kept : tier;
    }

    void MaterialTable::hold(TextureTable& textures, const MaterialIndex material, const HoldTier tier)
    {
        const HoldTier before = textureTierOf(material);
        holdRow(material, tier);
        retierTextures(textures, material, before);
    }

    void MaterialTable::retierTextures(TextureTable& textures, const MaterialIndex material, const HoldTier before)
    {
        const HoldTier after = textureTierOf(material);
        if (after == before)
            return;
        // The new tier held before the old is given back, so no texture passes through no hold at all.
        holdTextures(textures, rowAt(material), after);
        dropTextures(textures, rowAt(material), before);
    }

    void MaterialTable::holdTextures(TextureTable& textures, const Material& material, const HoldTier tier)
    {

        // The trace reads what a normal map's levels lost beside it (`normalMapSlopes`), which only
        // a slot taken as a normal map has: bound as any other data, it has no spread there, and the
        // companion a slot of no spread binds read as a loss is a gloss gone matt.
        Crash::contract(
            material.mNormal == sNoRow || textures.getRows()[material.mNormal].mEncoding == TextureEncoding::Normal,
            "a material's normal map in a slot not taken as one");

        forEachTexture(material, [&](const TextureIndex texture) { textures.hold(texture, tier); });
    }

    void MaterialTable::dropTextures(TextureTable& textures, const Material& material, const HoldTier tier)
    {
        forEachTexture(material, [&](const TextureIndex texture) { textures.drop(texture, tier); });
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
        assert(size() == 0 && "digests asked of a table that holds masks without one");
        assert(mMaskRuns.getEnd() == 0 && "digests asked of a table that holds masks without one");
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

    void MaterialTable::drop(TextureTable& textures, const MaterialIndex material, const HoldTier tier)
    {
        const HoldTier before = textureTierOf(material);
        if (!dropRow(material, tier))
        {
            retierTextures(textures, material, before);
            return;
        }

        Material& going = rowAt(material);
        assert(mAsks[material].empty() && "a material freed while something still asks it");

        // What it named goes with it, and before its layer run does: the run is what says which
        // textures those were, and it is about to be handed to an allocator that will let the next
        // chunk write over it.
        dropTextures(textures, going, before);

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
