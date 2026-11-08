#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpass.hpp>

namespace Rtx
{
    /// A file's levels under its finest, made again from that level (`ChainBuild`) in the file's own
    /// format and shape, as a pass: what the store keeps for every normal and data map, since the
    /// replacers' own chains are made with filters that are wrong for a map (`.notes/content-cache.md`,
    /// the chains' decisions). **Empty where the file's chain stands as it is**: a format `ChainBuild`
    /// does not rebuild, a file of one level, and a BC1 file with a cut.
    ///
    /// **The key is everything the build reads**: the file's format, encoding and level count, and
    /// its finest level's extent and bytes. Not its name, so one picture under two names is one entry,
    /// and not its coarser levels, which nothing here reads.
    class ChainPass
    {
    public:
        struct Input
        {
            const osg::Image& mImage;
            TextureEncoding mEncoding;
        };

        using Output = std::vector<std::byte>;

        static constexpr ContentPassId sPass = ContentPassId::Chain;
        static constexpr std::uint32_t sVersion = 1;
        static constexpr bool sStored = true;

        void digest(const Input& input, ContentDigest& digest);
        void run(const Input& input, Output& output);

    private:
        /// Describes `input`'s image into the scratch, and holds it, unless it is the one described.
        void describe(const Input& input);

        osg::ref_ptr<const osg::Image> mImage;
        TextureEncoding mEncoding = TextureEncoding::Colour;
        std::optional<TextureData> mFile;
        std::vector<MipLevel> mLevels;
        std::vector<std::byte> mTexels;
        ChainBuild mChain;
    };
}
