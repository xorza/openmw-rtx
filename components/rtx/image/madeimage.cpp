#include "madeimage.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

#include <osg/GL>

#include <components/sceneutil/embeddedimage.hpp>

namespace Rtx
{
    MadeImage::MadeImage(const osg::Image& file, const TexelLayout& laid, std::vector<std::byte> coarser)
        : mCoarser(std::move(coarser))
        , mLaid(laid)
    {
        const auto width = static_cast<std::uint32_t>(file.s());
        const auto height = static_cast<std::uint32_t>(file.t());
        std::size_t bytes = 0;
        for (std::uint32_t level = 1; bytes < mCoarser.size(); ++level)
        {
            const MipLevel at{ .mOffset = static_cast<std::uint32_t>(bytes),
                .mWidth = std::max(width >> level, 1u),
                .mHeight = std::max(height >> level, 1u) };
            mLevels.push_back(at);
            bytes += laid.levelBytes(at.mWidth, at.mHeight);
        }
        assert(bytes == mCoarser.size() && "a chain of another length than its levels");
    }

    TexelLayout MadeImage::laidLayout(const TextureFormat format)
    {
        return layoutOf(isWidened(format) ? TextureFormat::Rgba8Unorm : format);
    }

    osg::ref_ptr<const MadeImage> MadeImage::loose(const osg::Image& image, const std::span<const std::byte> levels)
    {
        const std::size_t finest = std::size_t(image.s()) * std::size_t(image.t()) * 4;
        assert(levels.size() >= finest && "a loose chain shorter than its finest level");

        auto data = std::make_unique_for_overwrite<unsigned char[]>(finest);
        std::memcpy(data.get(), levels.data(), finest);
        osg::ref_ptr<osg::Image> first = new osg::Image;
        first->setImage(
            image.s(), image.t(), 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, data.release(), osg::Image::USE_NEW_DELETE);
        first->setFileName(image.getFileName());
        if (const SceneUtil::EmbeddedImage* const stamped = SceneUtil::EmbeddedImage::find(image))
            first->setUserData(new SceneUtil::EmbeddedImage(*stamped));

        const std::span<const std::byte> coarser = levels.subspan(finest);
        osg::ref_ptr<MadeImage> made = new MadeImage(
            *first, layoutOf(TextureFormat::Rgba8Unorm), std::vector<std::byte>(coarser.begin(), coarser.end()));
        made->mOwnFinest = std::move(first);
        return made;
    }
}
