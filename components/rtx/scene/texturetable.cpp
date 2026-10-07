#include "texturetable.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <format>
#include <string>
#include <utility>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/image/texels.hpp>

namespace Rtx
{
    bool TextureTable::hasRoom()
    {
        if (mRows.getLiveCount() < sCapacity)
            return true;

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
        const Index index = mRows.take(std::move(row));
        mChanges.grow(mRows.size());
        mChanges.note(index, SlotNews::Arrived);

        return index;
    }

    Index TextureTable::add(const VFS::Path::NormalizedView path, const osg::Image* const image, const TextureWrap wrap,
        const TextureEncoding encoding)
    {
        const auto as = static_cast<std::size_t>(encoding);
        const auto at = static_cast<std::size_t>(wrap);

        auto known = mPathIndex.find(path);
        if (known != mPathIndex.end() && known->second[as][at] != sNoIndex)
            return known->second[as][at];

        if (!hasRoom())
            return sNoIndex;

        const TextureFormat format = image != nullptr ? readFormat(*image, encoding) : TextureFormat::Unnamed;
        const Index index = takeSlot(TextureRow{
            .mKind = TextureKind::File,
            .mPath = VFS::Path::Normalized(path),
            .mWrap = wrap,
            .mEncoding = encoding,
            .mImage = image,
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
        const TextureEncoding encoding)
    {
        const Index slot = add(path, &image, wrap, encoding);
        hold(slot);
        return slot;
    }

    Index TextureTable::findFile(const VFS::Path::NormalizedView path) const
    {
        const auto known = mPathIndex.find(path);
        if (known == mPathIndex.end())
            return sNoIndex;

        for (const Index slot : known->second[static_cast<std::size_t>(TextureEncoding::Colour)])
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

        mRows.hold(texture);
    }

    void TextureTable::drop(const Index texture)
    {
        if (texture >= sCapacity)
            return;

        if (!mRows.drop(texture))
            return;

        TextureRow& row = mRows.at(texture);

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
        mRows.free(texture);
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
