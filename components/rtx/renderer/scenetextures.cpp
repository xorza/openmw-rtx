#include "scenetextures.hpp"

#include <cassert>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/crashcatcher/crash.hpp>
#include <components/resource/imageheader.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/preprocess/preparedimages.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/sceneutil/embeddedimage.hpp>
#include <components/vfs/pathutil.hpp>

namespace Rtx
{
    namespace
    {
        /// What the upload reads of `row`, a file's slot: what the service read and made of it (`PreparedImages`),
        /// or, with none, the slot's own image as it stands, which holds its texels.
        PreparedImage findUpload(const TextureRow& row, PreparedImages* const prepared)
        {
            if (prepared != nullptr)
                return prepared->find(*row.mImage, row.mEncoding, row.mPartner.get());
            assert(!Resource::isHeader(*row.mImage) && "a header described with no service to read its texels");
            return PreparedImage{ .mFile = row.mImage };
        }

        bool isFile(const TextureRow& row)
        {
            return row.mKind == TextureKind::File && row.mImage != nullptr;
        }
    }

    void SceneTextures::describeAll(const SceneDesc& scene, PreparedImages* const prepared, const bool settled)
    {
        describe(scene, everyIndexBelow(scene.textures().getRows().size(), mEverything), prepared, settled);
    }

    void SceneTextures::describe(
        const SceneDesc& scene, std::span<const TextureIndex> slots, PreparedImages* const prepared, const bool settled)
    {
        mPending.clear();
        mStood.clear();

        // **Every file the slots need asked for first, and waited for**, where the caller is settled:
        // asked one at a time and each waited for, they would be read and made one after another.
        if (settled && prepared != nullptr)
        {
            for (const TextureIndex slot : slots)
                if (scene.textures().isLive(slot) && isFile(scene.textures().getRows()[slot]))
                    mFound.push_back(findUpload(scene.textures().getRows()[slot], prepared));
            prepared->flush();
        }
        mLevels.clear();
        mTexels.clear();
        mDescriptions.clear();
        mKept.clear();
        mRefusals.clear();

        mKept.reserve(slots.size());

        for (const TextureIndex slot : slots)
        {
            // A free slot is not a texture. `SceneDesc` empties one the last thing naming it
            // gave back and leaves it in the table until something takes it over; describing it
            // would build an image, a companion and a binding for a slot no material can
            // reach — and count it as a texture that arrived.
            if (!scene.textures().isLive(slot))
                continue;

            // A slot this renderer made rather than opened has no file to be asked for, and the
            // entry still has to exist because the description below is built from it.
            const TextureRow& row = scene.textures().getRows()[slot];
            Kept kept{ .mSlot = slot, .mEncoding = row.mEncoding };

            // **What the service read and made of a file**, never the file opened here: this runs on the
            // frame an arrival lands on, and a read is a lock and a disk. While it reads and makes, the slot
            // is named for its owner to stand again.
            const bool file = isFile(row);
            const PreparedImage made = file ? findUpload(row, prepared) : PreparedImage{};
            if (made.mPending)
                mPending.push_back(Pending{
                    .mSlot = slot, .mSource = row.mImage, .mEncoding = row.mEncoding, .mPartner = row.mPartner });

            // **A file stands in while it is read and its chain made**, and is handed to the device once, as
            // the image it keeps: stood as it is meanwhile, it was handed over twice, and a widened one was
            // widened here, on the frame an arrival lands on, into a copy nothing keeps. A chain made is
            // named by the slot's image and needs none of its texels.
            if (made.mImage != nullptr)
            {
                kept.mImage = row.mImage;
                kept.mFormat = row.mFormat;
                kept.mMade = made.mImage;
            }
            else if (file
                && (row.mEncoding == TextureEncoding::ConvertedSpecular || made.mPending
                    || (row.mEncoding == TextureEncoding::Normal && row.mFormat == TextureFormat::Bc5Unorm)))
                kept.mStandsIn = true;
            else if (file)
            {
                assert(made.mFile != nullptr && "a file handed over without its texels");
                kept.mImage = made.mFile;
                kept.mFormat = row.mFormat;
            }
            else if (row.mKind == TextureKind::File)
                kept.mImage = Err{ std::string(sNoImage) };
            else if (row.mKind == TextureKind::SpriteLight)
            {
                // Made on the device from the sprite texture's own slot, which the emitter holds
                // beside this one: a bake carries no bytes and is shaped like its source there.
                kept.mBakedFrom = scene.textures().findFile(row.mPath);
            }
            else
                kept.mGround = Ground{ .mMaterial = row.mGroundOf, .mGloss = row.mKind == TextureKind::GroundGloss };

            mKept.push_back(std::move(kept));
        }

        // Reserved before anything points into it: every description spans this one table, so it must
        // not grow while they are being taken, and every level count is known before the first is
        // built. Only a file puts levels here: a bake and a composite carry none, and the stand-in's
        // are its own.
        std::size_t levels = 0;
        std::size_t texels = 0;
        for (const Kept& kept : mKept)
            if (kept.mImage.isOk() && kept.mImage.value() != nullptr)
            {
                if (kept.mMade == nullptr)
                {
                    levels += kept.mImage.value()->getNumMipmapLevels();
                    texels += laidBytes(*kept.mImage.value(), kept.mFormat);
                }
                else
                    levels += kept.mMade->getLevels().size();
            }
        mLevels.reserve(levels);
        mTexels.reserve(texels);

        // What the contracts below are taken against: the reserve and the fill agree by argument
        // through branches that push a different number of levels each, and a growth is the failure
        // — in a release build too, where it would hand the backend spans into freed storage.
        const std::size_t reserved = mLevels.capacity();
        const std::size_t reservedTexels = mTexels.capacity();

        mDescriptions.reserve(mKept.size());
        for (const Kept& kept : mKept)
        {
            const TextureRow& row = scene.textures().getRows()[kept.mSlot];

            const Result<TextureData, std::string> described = describeKept(kept);
            TextureData data;
            if (described.isOk())
                data = described.value();
            else
            {
                mRefusals.push_back(
                    Refusal{ .mKind = Refused::Texture, .mName = row.getName(), .mWhy = described.error() });
                data = describeStandIn();
            }

            data.mSlot = kept.mSlot;
            data.mWrap = row.mWrap;
            if (row.mKind == TextureKind::File)
            {
                const bool whole = described.isOk() && data.mSource == TextureSource::File && !kept.mStandsIn;

                // **Whole is from its want** (`TextureData::topLevel`): a file its readers want from a coarser level
                // stands and stages none of the finer.
                const std::uint8_t want = scene.textures().getWant(kept.mSlot);
                const auto from = static_cast<std::uint8_t>(
                    data.mLevels.empty() ? 0 : std::min<std::size_t>(want, data.mLevels.size() - 1));
                if (whole)
                    data.mWant = from;
                mStood.push_back(Stood{ .mSlot = kept.mSlot,
                    .mBytes = whole ? data.bytesFrom(from) : 0,
                    .mMade = whole && kept.mMade != nullptr,
                    .mFrom = whole ? from : std::uint8_t{ 0 } });
            }
            mDescriptions.push_back(data);
        }

        mFound.clear();

        Crash::contract(mLevels.capacity() == reserved, "the level table grew while descriptions spanned it");
        Crash::contract(mTexels.capacity() == reservedTexels, "the laid texels grew while descriptions spanned them");
    }

    void SceneTextures::letGo()
    {
        mKept.clear();
        mFound.clear();
    }

    Result<TextureData, std::string> SceneTextures::describeKept(const Kept& kept)
    {
        if (kept.mStandsIn)
            return describeStandIn();

        if (!kept.mImage.isOk())
            return Err{ kept.mImage.error() };

        if (const osg::Image* image = kept.mImage.value().get())
        {
            // **A chain made** stands as it was made, under its file's name.
            if (kept.mMade != nullptr)
            {
                const MadeImage& made = *kept.mMade;
                const std::size_t at = mLevels.size();
                mLevels.insert(mLevels.end(), made.getLevels().begin(), made.getLevels().end());
                const MipLevel& top = made.getLevels().front();
                return TextureData{ .mFormat = made.getFormat(),
                    .mEncoding = kept.mEncoding,
                    .mWidth = top.mWidth,
                    .mHeight = top.mHeight,
                    .mBytes = made.getBytes(),
                    .mLevels = std::span<const MipLevel>(mLevels).subspan(at),
                    .mName = SceneUtil::EmbeddedImage::nameOf(*image) };
            }

            return describeImage(*image, kept.mFormat, kept.mEncoding, mLevels, mTexels);
        }

        if (kept.mBakedFrom.has_value())
        {
            // A source the table no longer holds is a bake of nothing.
            if (*kept.mBakedFrom == sNoRow)
                return Err{ "the texture it bakes is no longer held" };

            return TextureData{
                .mSource = TextureSource::SpriteBake,
                .mFrom = *kept.mBakedFrom,
                .mFormat = TextureFormat::Rgba8Unorm,
                .mEncoding = kept.mEncoding,
            };
        }

        // Flattened on the device in the placement after this arrival, from the chunk's own stack,
        // which the slot's row names however long ago it was made: the description carries the
        // chunk and no bytes.
        assert(kept.mGround.has_value() && "a slot with no file and no bake is a chunk's ground");
        const Ground& chunk = *kept.mGround;
        if (chunk.mGloss)
            return TextureData{
                .mSource = TextureSource::GroundGloss,
                .mFrom = chunk.mMaterial,
                .mFormat = TextureFormat::Bc7Unorm,
                .mEncoding = kept.mEncoding,
            };

        return TextureData{
            .mSource = TextureSource::GroundComposite,
            .mFrom = chunk.mMaterial,
            .mFormat = TextureFormat::Bc7Srgb,
            .mEncoding = kept.mEncoding,
        };
    }
}
