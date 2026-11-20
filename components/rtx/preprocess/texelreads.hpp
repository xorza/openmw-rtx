#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <osg/Image>
#include <osg/ref_ptr>

namespace Rtx
{
    /// Why a thread asked a header for its file's texels (`Resource::ImageHeader`): each ask its manager answers from
    /// its cache or by reading the file again.
    enum class TexelRead : std::uint8_t
    {
        /// A fact of the file no thread had read yet (`ThreadContent::factOf`).
        Fact,

        /// A slot's file, for its chain's key, its chain made again or its levels handed over (`TextureHands`).
        File,

        /// A converted slot's other file (`TextureRow::mPartner`).
        Partner,

        /// The loading screen's preparation of a chain (`ContentPreparation`).
        Preparation,
    };

    inline constexpr std::size_t sTexelReadCount = 4;

    /// The asks for a header's texels by why, with the bytes of the texels each was answered, how many chains made
    /// again were found whole in the store, so nothing of their file is read for the upload, and how many reads were
    /// cancelled before they started, their slot gone. Counted by the thread that asked, as `ContentStats` are.
    struct TexelReads
    {
        std::array<std::uint32_t, sTexelReadCount> mCounts{};
        std::array<std::uint64_t, sTexelReadCount> mBytes{};
        std::uint32_t mChainsFoundWhole = 0;
        std::uint32_t mCancelled = 0;

        std::uint32_t count(TexelRead why) const { return mCounts[static_cast<std::size_t>(why)]; }
        std::uint64_t bytes(TexelRead why) const { return mBytes[static_cast<std::size_t>(why)]; }

        TexelReads& operator+=(const TexelReads& other);
    };

    /// `image`'s texels (`Resource::texelsOf`), the ask counted in `reads` as `why` where `image` is a header: an image
    /// that holds its own texels is no ask of anyone's.
    osg::ref_ptr<const osg::Image> readTexels(const osg::Image& image, TexelRead why, TexelReads& reads);
}
