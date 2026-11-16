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

        settleResting();
        if (mResting.first() != sNoIndex)
        {
            releaseOldest();
            return true;
        }

        // Counted and not reported: a table does not reach the scene's `Refusals`, and
        // `SceneUploader` reports the limit on the hand-over that met it.
        ++mRefused;
        ++mRefusedArrivals;
        return false;
    }

    Index TextureTable::takeSlot(TextureRow row)
    {
        ++mRevision;

        // One size, so any freed slot will do — the row is written over wherever it sits, which
        // is what the arrivals list is for. The change list follows the rows in the same call,
        // because it is indexed by the slot.
        const Index index = takeRow(std::move(row));
        mChanges.grow(size());
        mChanges.note(index, SlotNews::Arrived);

        return index;
    }

    Index TextureTable::add(const VFS::Path::NormalizedView path, const osg::Image* const image, const TextureWrap wrap,
        const TextureEncoding encoding, const osg::Image* const partner)
    {
        assert((partner != nullptr) == (isConverted(encoding) && image != nullptr)
            && "a converted slot is two files, and no other slot is");
        const auto as = static_cast<std::size_t>(encoding);
        const auto at = static_cast<std::size_t>(wrap);

        auto known = mPathIndex.find(path);
        if (known != mPathIndex.end() && known->second[as][at] != sNoIndex)
        {
            const Index standing = known->second[as][at];
            if (rowAt(standing).mRest.mResting)
                wake(standing, image, partner);
            return standing;
        }

        if (!hasRoom())
            return sNoIndex;

        // Found again: the room may have been made by freeing a slot that rested, the path's own under
        // another wrap or encoding among them, which takes the path's entry out of the lookup.
        known = mPathIndex.find(path);

        const TextureFormat format = image != nullptr ? readFormat(*image, encoding) : TextureFormat::Unnamed;
        const Index index = takeSlot(TextureRow{
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
                slots.fill(sNoIndex);
            known = mPathIndex.emplace(path, none).first;
        }
        known->second[as][at] = index;

        return index;
    }

    Index TextureTable::take(const VFS::Path::NormalizedView path, const osg::Image& image, const TextureWrap wrap,
        const TextureEncoding encoding, const osg::Image* const partner)
    {
        const Index slot = add(path, &image, wrap, encoding, partner);
        hold(slot);
        return slot;
    }

    Index TextureTable::findFile(const VFS::Path::NormalizedView path) const
    {
        const auto known = mPathIndex.find(path);
        if (known == mPathIndex.end())
            return sNoIndex;

        for (const TextureEncoding encoding : { TextureEncoding::Colour, TextureEncoding::Painted })
            for (const Index slot : known->second[static_cast<std::size_t>(encoding)])
                if (slot != sNoIndex)
                    return slot;

        return sNoIndex;
    }

    Index TextureTable::takeBaked(TextureRow row)
    {
        if (!hasRoom())
            return sNoIndex;

        row.mWrap = TextureWrap::Clamp;
        return takeSlot(std::move(row));
    }

    Index TextureTable::addSpriteLight(const VFS::Path::NormalizedView source)
    {
        assert(!source.value().empty() && "a bake of no file is one nothing can find again");

        const auto known = mSpriteLightIndex.find(source);
        if (known != mSpriteLightIndex.end())
            return known->second;

        const Index index = takeBaked(TextureRow{
            .mKind = TextureKind::SpriteLight,
            .mPath = VFS::Path::Normalized(source),
            .mEncoding = TextureEncoding::Colour,
        });
        if (index != sNoIndex)
            mSpriteLightIndex.emplace(source, index);
        return index;
    }

    Index TextureTable::addGround(const Index material, const TextureKind kind)
    {
        assert(material != sNoIndex && "a ground composite names its material");
        assert((kind == TextureKind::GroundAlbedo || kind == TextureKind::GroundGloss) && "a ground is one of two");

        const std::size_t side = kind == TextureKind::GroundGloss ? 1 : 0;
        const auto known = mGroundIndex.find(material);
        if (known != mGroundIndex.end() && known->second[side] != sNoIndex)
            return known->second[side];

        const Index index = takeBaked(TextureRow{
            .mKind = kind,
            .mGroundOf = material,
            .mEncoding = kind == TextureKind::GroundGloss ? TextureEncoding::Data : TextureEncoding::Colour,
        });
        if (index == sNoIndex)
            return sNoIndex;

        GroundSlots& slots = known != mGroundIndex.end()
            ? known->second
            : mGroundIndex.emplace(material, GroundSlots{ sNoIndex, sNoIndex }).first->second;
        slots[side] = index;
        return index;
    }

    void TextureTable::hold(const Index texture)
    {
        if (texture >= sCapacity)
            return;

        holdRow(texture);
    }

    void TextureTable::drop(const Index texture)
    {
        if (texture >= sCapacity)
            return;

        if (!dropRow(texture))
            return;

        // A file the device stands whole rests; anything else, a file the device has not stood yet among
        // them, goes now, since nothing would take its rest back to the device.
        const TextureRow& row = rowAt(texture);
        if (row.mKind == TextureKind::File && row.mStoodBytes > 0)
            rest(texture);
        else
            release(texture);
    }

    void TextureTable::stand(const Index slot, const std::uint64_t bytes, const bool made)
    {
        TextureRow& row = rowAt(slot);
        assert(isLive(slot) && "a slot that stands no texture was stood");
        assert(!row.mRest.mResting && "a slot that rests was described");
        row.mStoodBytes = bytes;
        row.mStoodMade = made;
    }

    void TextureTable::fit(const Index slot, const std::uint64_t bytes)
    {
        assert((isLive(slot) || rowAt(slot).mRest.mResting) && "a slot the table does not hold fitted");
        mResting.fit(slot, bytes, placeOf());
    }

    bool TextureTable::releaseFitting(const std::uint64_t hole)
    {
        settleResting();
        const Index fitting = mResting.firstFitting(hole, placeOf());
        if (fitting == sNoIndex)
            return false;
        release(fitting);
        return true;
    }

    void TextureTable::releaseOldest()
    {
        settleResting();
        assert(mResting.first() != sNoIndex && "the oldest freed where nothing rests");
        release(mResting.first());
    }

    void TextureTable::rest(const Index slot)
    {
        TextureRow& row = rowAt(slot);
        assert(row.mKind == TextureKind::File && "only a file rests");

        // The census counts what the host keeps, and the host keeps nothing of a slot that rests.
        if (row.mImage != nullptr)
            mFormats.discount(row.mFormat, row.mImage->getNumMipmapLevels() > 1);
        row.mImage = nullptr;
        row.mPartner = nullptr;

        mResting.rest(slot, row.mStoodBytes, placeOf());
    }

    void TextureTable::wake(const Index slot, const osg::Image* const image, const osg::Image* const partner)
    {
        TextureRow& row = rowAt(slot);
        mResting.wake(slot, row.mStoodBytes, placeOf());

        // The taker's images, which a rebuild describes the slot from; the device stands them already.
        row.mImage = image;
        row.mPartner = partner;
        if (image != nullptr)
            mFormats.count(row.mFormat, image->getNumMipmapLevels() > 1, image->getPixelFormat());
    }

    void TextureTable::settleResting()
    {
        mResting.settle(mClock, placeOf());
    }

    void TextureTable::release(const Index texture)
    {
        TextureRow& row = rowAt(texture);
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
                held[static_cast<std::size_t>(row.mEncoding)][static_cast<std::size_t>(row.mWrap)] = sNoIndex;
                if (std::ranges::all_of(held, [](const auto& slots) {
                        return std::ranges::all_of(slots, [](const Index slot) { return slot == sNoIndex; });
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
                known->second[row.mKind == TextureKind::GroundGloss ? 1 : 0] = sNoIndex;
                if (known->second[0] == sNoIndex && known->second[1] == sNoIndex)
                    mGroundIndex.erase(known);
                break;
            }
        }

        row = TextureRow{};
        freeRow(texture);
        mChanges.note(texture, SlotNews::Freed);
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
                return std::format("the ground of material {}", mGroundOf);
            case TextureKind::GroundGloss:
                return std::format("the ground gloss of material {}", mGroundOf);
        }
        return {};
    }
}
