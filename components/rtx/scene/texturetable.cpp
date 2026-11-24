#include "texturetable.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <format>
#include <string>
#include <utility>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/image/textureformat.hpp>

namespace Rtx
{
    bool TextureTable::hasRoom()
    {
        if (getLiveCount() < sCapacity)
            return true;

        if (releaseOldest())
            return true;

        // Counted and not reported: a table does not reach the scene's `Refusals`, and
        // `SceneUploader` reports the limit on the hand-over that met it.
        ++mRefused;
        ++mRefusedArrivals;
        return false;
    }

    TextureIndex TextureTable::takeSlot(TextureRow row)
    {
        ++mRevision;

        // One size, so any freed slot will do — the row is written over wherever it sits, which
        // is what the arrivals list is for. The change list follows the rows in the same call,
        // because it is indexed by the slot.
        const TextureIndex index = takeRow(std::move(row));
        mChanges.grow(size());
        mChanges.note(index, SlotNews::Arrived);

        return index;
    }

    TextureIndex TextureTable::add(const VFS::Path::NormalizedView path, const osg::Image* const image,
        const TextureWrap wrap, const TextureEncoding encoding, const osg::Image* const partner)
    {
        assert((partner != nullptr) == (isConverted(encoding) && image != nullptr)
            && "a converted slot is two files, and no other slot is");
        const auto as = static_cast<std::size_t>(encoding);
        const auto at = static_cast<std::size_t>(wrap);

        auto known = mPathIndex.find(path);
        if (known != mPathIndex.end() && known->second[as][at] != sNoRow)
        {
            const TextureIndex standing = known->second[as][at];
            if (rowAt(standing).mRest.mResting)
                wake(standing, image, partner);
            return standing;
        }

        if (!hasRoom())
            return sNoRow;

        // Found again: the room may have been made by freeing a slot that rested, the path's own under
        // another wrap or encoding among them, which takes the path's entry out of the lookup.
        known = mPathIndex.find(path);

        const TextureFormat format = image != nullptr ? readFormat(*image, encoding) : TextureFormat::Unnamed;
        const TextureIndex index = takeSlot(TextureRow{
            .mKind = TextureKind::File,
            .mPath = VFS::Path::Normalized(path),
            .mWrap = wrap,
            .mEncoding = encoding,
            .mImage = image,
            .mPartner = partner,
            .mFormat = format,
        });

        if (image != nullptr)
            mFormats.count(format, image->getNumMipmapLevels() > 1, image->getPixelFormat());

        if (known == mPathIndex.end())
        {
            FileSlots none;
            for (auto& slots : none)
                slots.fill(sNoRow);
            known = mPathIndex.emplace(path, none).first;
        }
        known->second[as][at] = index;

        return index;
    }

    TextureIndex TextureTable::take(const HoldTier tier, const VFS::Path::NormalizedView path, const osg::Image& image,
        const TextureWrap wrap, const TextureEncoding encoding, const osg::Image* const partner)
    {
        const TextureIndex slot = add(path, &image, wrap, encoding, partner);
        hold(slot, tier);
        return slot;
    }

    TextureIndex TextureTable::findFile(const VFS::Path::NormalizedView path) const
    {
        const auto known = mPathIndex.find(path);
        if (known == mPathIndex.end())
            return sNoRow;

        for (const TextureIndex slot : known->second[static_cast<std::size_t>(TextureEncoding::Colour)])
            if (slot != sNoRow && !rowAt(slot).mRest.mResting)
                return slot;

        return sNoRow;
    }

    TextureIndex TextureTable::takeBaked(TextureRow row)
    {
        if (!hasRoom())
            return sNoRow;

        row.mWrap = TextureWrap::Clamp;
        return takeSlot(std::move(row));
    }

    TextureIndex TextureTable::addSpriteLight(const VFS::Path::NormalizedView source)
    {
        assert(!source.value().empty() && "a bake of no file is one nothing can find again");

        const auto known = mSpriteLightIndex.find(source);
        if (known != mSpriteLightIndex.end())
            return known->second;

        const TextureIndex index = takeBaked(TextureRow{
            .mKind = TextureKind::SpriteLight,
            .mPath = VFS::Path::Normalized(source),
            .mEncoding = TextureEncoding::Colour,
        });
        if (index != sNoRow)
            mSpriteLightIndex.emplace(source, index);
        return index;
    }

    TextureIndex TextureTable::addGround(const MaterialIndex material, const TextureKind kind)
    {
        assert(material != sNoRow && "a ground composite names its material");
        assert((kind == TextureKind::GroundAlbedo || kind == TextureKind::GroundGloss) && "a ground is one of two");

        const std::size_t side = kind == TextureKind::GroundGloss ? 1 : 0;
        const auto known = mGroundIndex.find(material);
        if (known != mGroundIndex.end() && known->second[side] != sNoRow)
            return known->second[side];

        const TextureIndex index = takeBaked(TextureRow{
            .mKind = kind,
            .mGroundOf = material,
            .mEncoding = kind == TextureKind::GroundGloss ? TextureEncoding::Data : TextureEncoding::Colour,
        });
        if (index == sNoRow)
            return sNoRow;

        GroundSlots& slots = known != mGroundIndex.end()
            ? known->second
            : mGroundIndex.emplace(material, GroundSlots{ sNoRow, sNoRow }).first->second;
        slots[side] = index;
        return index;
    }

    void TextureTable::hold(const TextureIndex texture, const HoldTier tier)
    {
        if (texture.at() >= sCapacity)
            return;

        holdRow(texture, tier);
    }

    void TextureTable::drop(const TextureIndex texture, const HoldTier tier)
    {
        if (texture.at() >= sCapacity)
            return;

        if (!dropRow(texture, tier))
            return;

        // **A file the device stands rests, keeping its slot**, so residency may keep what stands and a take of it
        // finds it there; anything else goes now, a bake among it, since its source stands beside it.
        const TextureRow& row = rowAt(texture);
        if (row.mKind == TextureKind::File && row.mStands)
            rest(texture);
        else
            release(texture);
    }

    void TextureTable::setStanding(const TextureIndex slot, const bool stands)
    {
        assert(isLive(slot) && "a slot said to stand that the table holds no more");
        rowAt(slot).mStands = stands;
    }

    void TextureTable::ask(const TextureIndex texture, const std::size_t level)
    {
        if (texture.at() >= sCapacity)
            return;

        assert(isLive(texture) && "an ask of a slot that stands no texture");
        TextureRow& row = rowAt(texture);
        row.mAsks.ask(level);
    }

    void TextureTable::unask(const TextureIndex texture, const std::size_t level)
    {
        if (texture.at() >= sCapacity)
            return;

        assert(isLive(texture) && "an ask taken back of a slot that stands no texture");
        TextureRow& row = rowAt(texture);
        row.mAsks.unask(level);
    }

    bool TextureTable::releaseOldest()
    {
        settleResting();
        for (TextureIndex slot = mResting.first(); slot != sNoRow; slot = rowAt(slot).mRest.mAfter)
        {
            const TextureRow& row = rowAt(slot);
            if (row.mEncoding == TextureEncoding::Colour && mSpriteLightIndex.contains(row.mPath))
                continue;
            release(slot);
            return true;
        }
        return false;
    }

    void TextureTable::rest(const TextureIndex slot)
    {
        TextureRow& row = rowAt(slot);
        assert(row.mKind == TextureKind::File && "only a file rests");

        // The census counts what the host keeps, and the host keeps nothing of a slot that rests.
        if (row.mImage != nullptr)
            mFormats.discount(row.mFormat, row.mImage->getNumMipmapLevels() > 1);
        row.mImage = nullptr;
        row.mPartner = nullptr;

        mResting.rest(slot, placeOf());
        ++mRoomMade;
    }

    void TextureTable::wake(const TextureIndex slot, const osg::Image* const image, const osg::Image* const partner)
    {
        TextureRow& row = rowAt(slot);
        mResting.wake(slot, placeOf());

        // The taker's images, which a rebuild describes the slot from; the device stands them already.
        row.mImage = image;
        row.mPartner = partner;
        if (image != nullptr)
            mFormats.count(row.mFormat, image->getNumMipmapLevels() > 1, image->getPixelFormat());
    }

    void TextureTable::settleResting()
    {
        mResting.settle(placeOf());
    }

    void TextureTable::release(const TextureIndex texture)
    {
        TextureRow& row = rowAt(texture);
        assert(row.mAsks.empty() && "a slot freed while something still reads it");
        if (row.mRest.mResting)
        {
            // Out of the rest first, which leaves the census as a slot that rests left it.
            wake(texture, nullptr, nullptr);
        }

        // The name leaves the lookup with the slot, or the next reference to it resolves to a slot
        // nothing is standing in.
        switch (row.mKind)
        {
            case TextureKind::File:
            {
                const auto known = mPathIndex.find(row.mPath);
                Crash::contract(known != mPathIndex.end(), "a file slot the path index does not know");
                FileSlots& held = known->second;
                held[static_cast<std::size_t>(row.mEncoding)][static_cast<std::size_t>(row.mWrap)] = sNoRow;
                if (std::ranges::all_of(held, [](const auto& slots) {
                        return std::ranges::all_of(slots, [](const TextureIndex slot) { return slot == sNoRow; });
                    }))
                    mPathIndex.erase(known);
                if (row.mImage != nullptr)
                    mFormats.discount(row.mFormat, row.mImage->getNumMipmapLevels() > 1);
                break;
            }
            case TextureKind::SpriteLight:
                mSpriteLightIndex.erase(row.mPath);
                break;
            case TextureKind::GroundAlbedo:
            case TextureKind::GroundGloss:
            {
                const auto known = mGroundIndex.find(row.mGroundOf);
                Crash::contract(known != mGroundIndex.end(), "a ground slot the ground index does not know");
                known->second[row.mKind == TextureKind::GroundGloss ? 1 : 0] = sNoRow;
                if (known->second[0] == sNoRow && known->second[1] == sNoRow)
                    mGroundIndex.erase(known);
                break;
            }
        }

        row = TextureRow{};
        freeRow(texture);
        mChanges.note(texture, SlotNews::Freed);
        ++mRoomMade;
    }

    std::string TextureRow::getName() const
    {
        switch (mKind)
        {
            case TextureKind::File:
                return std::string(mPath.value());
            case TextureKind::SpriteLight:
                return std::format("the light of {}", mPath.value());
            case TextureKind::GroundAlbedo:
                return std::format("the ground of material {}", mGroundOf.at());
            case TextureKind::GroundGloss:
                return std::format("the ground gloss of material {}", mGroundOf.at());
        }
        return {};
    }
}
