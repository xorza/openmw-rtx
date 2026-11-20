#include "filekeys.hpp"

#include <cassert>
#include <cstdint>
#include <span>
#include <string>

#include <osg/Image>

#include <components/misc/result.hpp>
#include <components/resource/imageheader.hpp>
#include <components/rtx/image/alphaimage.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/texturedata.hpp>

#include "headerderived.hpp"

namespace Rtx
{
    ContentStore::Key hashFile(const osg::Image& texels, TexelScratch& scratch)
    {
        assert(!Resource::isHeader(texels) && "a header's key hashed, which its derivation holds");

        // Read as data, whose description lays every level as the file holds it: the encoding a slot reads it as is
        // the pass's to add, not the file's.
        scratch.mLevels.clear();
        scratch.mTexels.clear();
        const Result<TextureData, std::string> read
            = describeImage(texels, TextureEncoding::Data, scratch.mLevels, scratch.mTexels);

        ContentStore::Digest digest("file", sFileKeyVersion);
        digest.add(read.isOk());
        if (!read.isOk())
        {
            // **Refused as data is not refused as every encoding**, which reads another format: the image keyed as
            // OpenSceneGraph holds it, so two files a slot's encoding describes never meet under one key.
            digest.add(texels.s());
            digest.add(texels.t());
            digest.add(texels.r());
            digest.add(texels.getInternalTextureFormat());
            digest.add(texels.getPixelFormat());
            digest.add(texels.getDataType());
            digest.add(texels.getPacking());
            digest.add(std::span<const std::uint32_t>(texels.getMipmapLevels()));
            if (texels.data() != nullptr)
                digest.add(std::span<const std::uint8_t>(texels.data(), texels.getTotalSizeInBytesIncludingMipmaps()));
        }
        else
        {
            const TextureData& file = read.value();
            assert(file.levelsFit() && "a key of a description short of its bytes");
            digest.add(file.mFormat);
            digest.add(static_cast<std::uint32_t>(file.mLevels.size()));
            for (std::uint32_t level = 0; level < file.mLevels.size(); ++level)
            {
                digest.add(file.mLevels[level].mWidth);
                digest.add(file.mLevels[level].mHeight);
                digest.add(file.bytesOf(level));
            }
        }
        scratch.mLevels.clear();
        scratch.mTexels.clear();
        return digest.getKey();
    }

    ContentStore::Key FileKeys::of(const osg::Image& image)
    {
        if (const auto* const header = dynamic_cast<const Resource::ImageHeader*>(&image))
        {
            const std::optional<FileFacts>& derived = header->derived<HeaderDerived>().mFacts;
            assert(derived.has_value() && "a header its manager handed out underived");
            return derived->mFile;
        }
        if (&image == mNamed)
            return mNamedKey;
        return hashFile(image, mScratch);
    }

    void FileKeys::name(const osg::Image& texels, const ContentStore::Key& key)
    {
        assert(mNamed == nullptr && "a second image named before the first was forgotten");
        mNamed = &texels;
        mNamedKey = key;
    }

    void FileKeys::forget()
    {
        mNamed = nullptr;
    }
}
