#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/image/alphaimage.hpp>
#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpass.hpp>

#include "chainscratch.hpp"
#include <components/rtx/preprocess/texture/chainpass.hpp>

namespace Rtx
{
    /// One texel of a classic pair as metal-roughness: Khronos' converter of the specular-glossiness
    /// workflow (`KHR_materials_pbrSpecularGlossiness`, "convert-between-workflows"), its colours in
    /// linear light.
    struct ConvertedTexel
    {
        /// The base colour, linear.
        osg::Vec3f mBase;

        float mMetal = 0.0f;
    };

    /// `diffuse` and `specular`, a classic pair's delit diffuse and highlight at one texel, as Khronos'
    /// `ConvertToMetallicRoughness` solves them: the metalness from the two's perceived brightness, a
    /// highlight under the dielectric's reflectance a dielectric, and the base colour the blend of what
    /// each answers for, weighed by the metalness squared.
    ConvertedTexel convertTexel(const osg::Vec3f& diffuse, const osg::Vec3f& specular);

    /// A classic pair's base colour or metal-roughness map, made of the pair's diffuse and its classic
    /// `_spec` (`docs/rtx/architecture.md`, "One specular layout, a classic map converted"): every
    /// texel of the job's own file converted with its partner's nearest texel (`convertTexel`), the
    /// diffuse delit first as a painted colour is (`ChainBuild`), and the roughness the exponent's
    /// (`roughnessOfExponent`), the occlusion one. The whole chain is made to a texel from the
    /// converted finest: a base colour in the diffuse's own block format where it has one and no cut,
    /// as a painted colour is kept, and in BC7 otherwise; a metal-roughness map in BC7.
    ///
    /// **The key is everything the run reads**: the encoding, the wrap the diffuse is delit under, and
    /// each file's format, finest extent and finest level's bytes, the one level it reads of either.
    class ConversionPass
    {
    public:
        struct Input
        {
            /// The slot's own file: the diffuse for a base colour, the `_spec` for a metal-roughness map.
            const osg::Image& mImage;

            /// The other file of the pair.
            const osg::Image& mPartner;

            /// `TextureEncoding::ConvertedColour` or `TextureEncoding::ConvertedSpecular`.
            TextureEncoding mEncoding;

            TextureWrap mWrap = TextureWrap::Repeat;
        };

        using Output = MadeChain;

        /// @param build the chain build this pass and `ChainPass` share (`ChainPass`'s reason), which
        ///        delits the diffuse and then makes the converted chain.
        explicit ConversionPass(ChainBuild& build)
            : mChain(build)
        {
        }

        static constexpr ContentPassId sPass = ContentPassId::Conversion;
        static constexpr std::uint32_t sVersion = 4;
        static constexpr bool sStored = true;

        void digest(const Input& input, ContentDigest& digest);
        void run(const Input& input, Output& output);

        /// Lets go of both images `digest` described (`ChainPass::release`).
        void release();

        /// What a run of `input` holds of each buffer at most, read off its two images' headers alone
        /// (`ChainPass::scratchOf`): both files laid, the `_spec`'s exponents, band and highlights, the
        /// converted level, the larger of its two builds, and the chain it makes.
        static ChainScratch scratchOf(const Input& input);

        /// What its own buffers hold now, the build's aside.
        ChainScratch capacity() const;

        /// Gives its own buffers' room back.
        void letGo();

    private:
        /// Describes `input`'s two files into the scratch, the diffuse as a painted colour and the
        /// `_spec` as a colour, unless they are the ones described.
        void describe(const Input& input);

        osg::ref_ptr<const osg::Image> mImage;
        osg::ref_ptr<const osg::Image> mPartner;
        TextureEncoding mEncoding = TextureEncoding::ConvertedColour;
        TextureWrap mWrap = TextureWrap::Repeat;

        std::optional<TextureData> mDiffuse;
        std::optional<TextureData> mSpecular;
        std::vector<MipLevel> mDiffuseLevels;
        std::vector<std::byte> mDiffuseTexels;
        std::vector<MipLevel> mSpecularLevels;
        std::vector<std::byte> mSpecularTexels;

        AlphaImage mGloss;
        std::vector<osg::Vec3f> mBand;
        std::vector<osg::Vec3f> mHighlights;

        /// The converted finest level, four bytes a texel, and the chain made of it.
        std::vector<std::byte> mConverted;
        std::vector<MipLevel> mConvertedLevel;
        ChainBuild& mChain;
    };
}
