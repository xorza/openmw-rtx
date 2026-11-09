#include "conversionpass.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>

#include <components/misc/result.hpp>
#include <components/rtx/image/colour.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/shaders/brdf.h>
#include <components/rtx/shaders/look.h>

namespace Rtx
{
    namespace
    {
        /// Khronos' guard against a division by nought where the metalness is nought or one.
        constexpr float sEpsilon = 1e-6f;

        /// Khronos' perceived brightness: `sqrt(0.299 r² + 0.587 g² + 0.114 b²)`.
        float perceivedBrightness(const osg::Vec3f& colour)
        {
            return std::sqrt(
                0.299f * colour.x() * colour.x() + 0.587f * colour.y() * colour.y() + 0.114f * colour.z() * colour.z());
        }

        /// Khronos' `solveMetallic`: the root of the quadratic the brightness of the base colour that
        /// gives both the diffuse and the highlight solves to, nought under a dielectric's reflectance.
        float solveMetallic(float diffuse, float specular, float oneMinusSpecularStrength)
        {
            const float dielectric = Shaders::DIELECTRIC_F0;
            if (specular < dielectric)
                return 0.0f;
            const float a = dielectric;
            const float b = diffuse * oneMinusSpecularStrength / (1.0f - dielectric) + specular - 2.0f * dielectric;
            const float c = dielectric - specular;
            const float discriminant = std::max(b * b - 4.0f * a * c, 0.0f);
            return std::clamp((-b + std::sqrt(discriminant)) / (2.0f * a), 0.0f, 1.0f);
        }

        /// The nearest texel of a `width` by `height` level to the centre of texel `x`, `y` of one of
        /// `across` by `down`.
        std::uint32_t nearest(std::uint32_t at, std::uint32_t of, std::uint32_t extent)
        {
            return std::min(static_cast<std::uint32_t>((static_cast<double>(at) + 0.5) * extent / of), extent - 1);
        }

        /// What the run reads of `file`: its format and its finest level, the one level a conversion
        /// reads of either file.
        void addFinest(const TextureData& file, ContentDigest& digest)
        {
            digest.add(file.mFormat);
            digest.add(file.mLevels.front().mWidth);
            digest.add(file.mLevels.front().mHeight);
            digest.add(file.bytesOf(0));
        }

        /// `file` with its finest level alone, as a reader of nothing else takes it.
        TextureData finestOf(const TextureData& file)
        {
            TextureData finest = file;
            finest.mLevels = file.mLevels.first(1);
            finest.mBytes = file.mBytes.first(file.mLevels.front().mOffset + file.bytesOf(0).size());
            finest.mCoarser = {};
            return finest;
        }
    }

    ConvertedTexel convertTexel(const osg::Vec3f& diffuse, const osg::Vec3f& specular)
    {
        const float dielectric = Shaders::DIELECTRIC_F0;
        const float oneMinusSpecularStrength = 1.0f - std::max({ specular.x(), specular.y(), specular.z() });
        const float metal
            = solveMetallic(perceivedBrightness(diffuse), perceivedBrightness(specular), oneMinusSpecularStrength);

        const osg::Vec3f fromDiffuse
            = diffuse * (oneMinusSpecularStrength / (1.0f - dielectric) / std::max(1.0f - metal, sEpsilon));
        const osg::Vec3f fromSpecular
            = (specular - osg::Vec3f(dielectric, dielectric, dielectric) * (1.0f - metal)) / std::max(metal, sEpsilon);
        const float blend = metal * metal;
        osg::Vec3f base = fromDiffuse * (1.0f - blend) + fromSpecular * blend;
        for (int channel = 0; channel < 3; ++channel)
            base[channel] = std::clamp(base[channel], 0.0f, 1.0f);
        return ConvertedTexel{ .mBase = base, .mMetal = metal };
    }

    void ConversionPass::describe(const Input& input)
    {
        if (mImage.get() == &input.mImage && mPartner.get() == &input.mPartner && mEncoding == input.mEncoding
            && mWrap == input.mWrap)
            return;

        mImage = &input.mImage;
        mPartner = &input.mPartner;
        mEncoding = input.mEncoding;
        mWrap = input.mWrap;

        const bool colour = input.mEncoding == TextureEncoding::ConvertedColour;
        const osg::Image& diffuse = colour ? input.mImage : input.mPartner;
        const osg::Image& specular = colour ? input.mPartner : input.mImage;

        mDiffuseLevels.clear();
        mDiffuseTexels.clear();
        const Result<TextureData, std::string> readDiffuse
            = describeImage(diffuse, TextureEncoding::Painted, mDiffuseLevels, mDiffuseTexels);
        mDiffuse = readDiffuse.isOk() ? std::optional(readDiffuse.value()) : std::nullopt;
        if (mDiffuse.has_value())
            mDiffuse->mWrap = input.mWrap;

        mSpecularLevels.clear();
        mSpecularTexels.clear();
        const Result<TextureData, std::string> readSpecular
            = describeImage(specular, TextureEncoding::Colour, mSpecularLevels, mSpecularTexels);
        mSpecular = readSpecular.isOk() ? std::optional(readSpecular.value()) : std::nullopt;
    }

    void ConversionPass::digest(const Input& input, ContentDigest& digest)
    {
        describe(input);
        const bool converts = mDiffuse.has_value() && mSpecular.has_value() && ChainBuild::rebuilds(*mDiffuse)
            && readsColour(*mSpecular);
        digest.add(converts);
        if (!converts)
            return;

        digest.add(input.mEncoding);
        digest.add(input.mWrap);
        addFinest(*mDiffuse, digest);
        addFinest(*mSpecular, digest);
    }

    void ConversionPass::run(const Input& input, Output& output)
    {
        describe(input);
        output.mForm = ChainForm::Stands;
        output.mBytes.clear();
        if (mDiffuse.has_value() && mSpecular.has_value() && ChainBuild::rebuilds(*mDiffuse) && readsColour(*mSpecular))
        {
            const TextureData& diffuse = *mDiffuse;
            const TextureData& specular = *mSpecular;
            const bool colour = input.mEncoding == TextureEncoding::ConvertedColour;

            // The diffuse delit, at its finest level, as a painted colour's chain is.
            // **One build for both chains**: the delit diffuse is read into the converted level and whether
            // it holds a cut before the build makes the converted chain over it.
            mChain.build(diffuse, 1);
            const bool cut = mChain.holdsCut(diffuse);
            const MipLevel lit = mChain.getLevel(0);
            const std::span<const std::byte> delit = mChain.getTexels(0);

            // The highlight, display-encoded as the artist saw it, and the exponent in its alpha.
            const MipLevel& shine = specular.mLevels.front();
            mGloss.build(finestOf(specular));
            mHighlights.resize(std::size_t{ shine.mWidth } * shine.mHeight);
            for (std::uint32_t row = 0; row < shine.mHeight; row += 4)
            {
                readTexelBand(specular, shine, row / 4, mBand);
                std::ranges::copy(mBand, mHighlights.begin() + std::ptrdiff_t{ row } * shine.mWidth);
            }

            // **Every texel of the slot's own file, with its partner's nearest.**
            const MipLevel& own = colour ? lit : shine;
            mConverted.assign(std::size_t{ own.mWidth } * own.mHeight * 4, std::byte{ 0 });
            for (std::uint32_t y = 0; y < own.mHeight; ++y)
                for (std::uint32_t x = 0; x < own.mWidth; ++x)
                {
                    const std::uint32_t dx = colour ? x : nearest(x, own.mWidth, lit.mWidth);
                    const std::uint32_t dy = colour ? y : nearest(y, own.mHeight, lit.mHeight);
                    const std::uint32_t sx = colour ? nearest(x, own.mWidth, shine.mWidth) : x;
                    const std::uint32_t sy = colour ? nearest(y, own.mHeight, shine.mHeight) : y;

                    const std::size_t from = (std::size_t{ dy } * lit.mWidth + dx) * 4;
                    osg::Vec3f light;
                    for (int channel = 0; channel < 3; ++channel)
                        light[channel] = toLinear(std::to_integer<std::uint8_t>(delit[from + std::size_t(channel)]));
                    const osg::Vec3f highlight = toLinear(mHighlights[std::size_t{ sy } * shine.mWidth + sx]);
                    const ConvertedTexel converted = convertTexel(light, highlight);

                    const std::size_t into = (std::size_t{ y } * own.mWidth + x) * 4;
                    const auto byteOf = [](float unit) {
                        return static_cast<std::byte>(std::clamp(std::lround(unit * 255.0f), 0L, 255L));
                    };
                    if (colour)
                    {
                        for (int channel = 0; channel < 3; ++channel)
                            mConverted[into + std::size_t(channel)] = byteOf(toEncoded(converted.mBase[channel]));
                        mConverted[into + 3] = delit[from + 3];
                    }
                    else
                    {
                        const float exponent = static_cast<float>(mGloss.at(0, sx, sy));
                        mConverted[into + 0] = byteOf(converted.mMetal);
                        mConverted[into + 1] = byteOf(Shaders::roughnessOfExponent(exponent));
                        mConverted[into + 2] = std::byte{ 255 };
                        mConverted[into + 3] = std::byte{ 255 };
                    }
                }

            // The chain of the converted finest, to a texel, as a file of four loose bytes.
            mConvertedLevel.assign(1, MipLevel{ .mOffset = 0, .mWidth = own.mWidth, .mHeight = own.mHeight });
            const TextureData made{
                .mFormat = colour ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm,
                .mEncoding = colour ? TextureEncoding::Colour : TextureEncoding::Data,
                .mWidth = own.mWidth,
                .mHeight = own.mHeight,
                .mBytes = mConverted,
                .mLevels = mConvertedLevel,
            };
            mChain.build(made, levelsTo1x1(own.mWidth, own.mHeight));

            // A base colour in the diffuse's own block format where it has one and holds no cut, as a
            // painted colour is kept; every other in BC7.
            const bool blocked = layoutOf(diffuse.mFormat).isBlocked();
            if (colour && blocked && !cut)
            {
                TextureData asFile = made;
                asFile.mFormat = diffuse.mFormat;
                output.mForm = ChainForm::Own;
                mChain.encode(asFile, 0, output.mBytes);
            }
            else
            {
                output.mForm = ChainForm::Bc7;
                mChain.encodeBc7(input.mEncoding, output.mBytes);
            }
        }

        release();
    }

    void ConversionPass::release()
    {
        mImage = nullptr;
        mPartner = nullptr;
        mDiffuse.reset();
        mSpecular.reset();
    }
}
