#include "chainbuild.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <mutex>
#include <span>

#include <rgbcx/bc7enc.h>
#include <rgbcx/rgbcx.h>

#include <components/rtx/common/letgo.hpp>
#include <components/rtx/shaders/halving.h>
#include <components/rtx/shaders/scene.h>

#include "colour.hpp"
#include "shadingmap.hpp"
#include "texels.hpp"
#include "textureformat.hpp"
#include "texturewrap.hpp"

namespace Rtx
{
    namespace
    {
        /// How hard rgbcx searches a block: the level its author names for when in doubt, among the
        /// ones that match squish and NVTT, since a chain is built once and kept.
        constexpr std::uint32_t sEncodeLevel = 10;

        constexpr std::size_t sTexel = 4;

        bool isBc2(TextureFormat format)
        {
            return format == TextureFormat::Bc2Srgb || format == TextureFormat::Bc2Unorm;
        }

        bool isBc3(TextureFormat format)
        {
            return format == TextureFormat::Bc3Srgb || format == TextureFormat::Bc3Unorm;
        }

        bool isLoose(TextureFormat format)
        {
            return format == TextureFormat::Rgba8Srgb || format == TextureFormat::Rgba8Unorm
                || format == TextureFormat::Bgra8Srgb || format == TextureFormat::Bgra8Unorm;
        }

        /// rgbcx's tables, made once for the process: its `init` writes globals and is not
        /// thread-safe, which the library states, so the outside world's state is set here and once.
        void initEncoders()
        {
            static std::once_flag made;
            std::call_once(made, [] {
                rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
                bc7enc_compress_block_init();
            });
        }

        /// Four normals worked together, a component an array: **every step is written a lane at a time
        /// over the four**, so a compiler works them as one vector, and a normal's square root and its
        /// division do not wait their turn behind the normal before it, as they do one normal at a time.
        struct FourNormals
        {
            std::array<float, 4> mX{};
            std::array<float, 4> mY{};
            std::array<float, 4> mZ{};

            osg::Vec3f at(std::size_t lane) const { return osg::Vec3f(mX[lane], mY[lane], mZ[lane]); }
        };

        /// The direction each of four decoded normals points, and straight up where one points none: a
        /// texel of (0.5, 0.5, 0.5), or a box of normals that cancel.
        FourNormals unitNormals(const FourNormals& decoded)
        {
            FourNormals unit;
            for (std::size_t lane = 0; lane < 4; ++lane)
            {
                const float x = decoded.mX[lane];
                const float y = decoded.mY[lane];
                const float z = decoded.mZ[lane];
                const float length = std::sqrt(x * x + y * y + z * z);

                // Divided whether or not it points anywhere, and the quotient left unread where it points
                // nowhere: a lane that branches is a lane worked alone.
                const bool points = length > 0.0f;
                const float across = x / length;
                const float along = y / length;
                const float up = z / length;
                unit.mX[lane] = points ? across : 0.0f;
                unit.mY[lane] = points ? along : 0.0f;
                unit.mZ[lane] = points ? up : 1.0f;
            }
            return unit;
        }

        /// A unit normal's component as the byte `2 c − 1` decodes back to it.
        std::byte quantiseNormal(float component)
        {
            return unitByte((component + 1.0f) * 0.5f);
        }

        /// The three bytes of each of four texels of a normal map: the direction of its mean, a component
        /// a byte.
        std::array<std::array<std::byte, 3>, 4> layDirections(const FourNormals& means)
        {
            const FourNormals directions = unitNormals(means);
            std::array<std::array<std::byte, 3>, 4> laid;
            for (std::size_t lane = 0; lane < 4; ++lane)
                laid[lane] = { quantiseNormal(directions.mX[lane]), quantiseNormal(directions.mY[lane]),
                    quantiseNormal(directions.mZ[lane]) };
            return laid;
        }

        /// What each byte decodes to, a byte over 255: a height as the box of the level under reads it.
        constexpr std::array<float, 256> sUnitOfByte = [] {
            std::array<float, 256> units{};
            for (std::size_t byte = 0; byte < units.size(); ++byte)
                units[byte] = static_cast<float>(byte) / 255.0f;
            return units;
        }();

        /// The sixteen texels of block `bx`, `by` of `level`, RGBA, the edge's texel standing in for a
        /// texel past a level smaller than a block, as a block-compressed file pads it.
        std::array<std::uint8_t, 64> blockAt(
            std::span<const std::byte> texels, const MipLevel& level, std::uint32_t bx, std::uint32_t by)
        {
            std::array<std::uint8_t, 64> block{};
            for (std::uint32_t y = 0; y < 4; ++y)
                for (std::uint32_t x = 0; x < 4; ++x)
                {
                    const std::uint32_t sx = std::min(bx * 4 + x, level.mWidth - 1);
                    const std::uint32_t sy = std::min(by * 4 + y, level.mHeight - 1);
                    const std::size_t from = (std::size_t{ sy } * level.mWidth + sx) * sTexel;
                    for (std::size_t channel = 0; channel < sTexel; ++channel)
                        block[(y * 4 + x) * sTexel + channel] = std::to_integer<std::uint8_t>(texels[from + channel]);
                }
            return block;
        }
    }

    ChainBuild::Scratch ChainBuild::scratchOf(const std::uint32_t width, const std::uint32_t height,
        const std::uint32_t fileLevels, const TextureEncoding encoding, const std::uint32_t levels)
    {
        // The levels `build` reads and lays out, as it reads and lays them: a painted colour's own up to
        // `levels`, every other file's finest alone, and `levels` laid four bytes a texel.
        const std::uint32_t read
            = encoding == TextureEncoding::Painted ? std::min(levels, std::max(fileLevels, 1u)) : 1u;
        const auto texelsOf = [&](std::uint32_t level) {
            return std::size_t{ std::max(width >> level, 1u) } * std::max(height >> level, 1u);
        };
        Scratch scratch;
        for (std::uint32_t level = 0; level < read; ++level)
            scratch.mAlpha += texelsOf(level);
        for (std::uint32_t level = 0; level < levels; ++level)
            scratch.mTexels += texelsOf(level) * sTexel;
        // A normal map is read through no band: its texels are laid as its blocks are read.
        if (encoding == TextureEncoding::Normal)
            for (std::uint32_t level = 0; level < levels; ++level)
                scratch.mRing += std::size_t{ sRingRows } * std::max(width >> level, 1u) * sizeof(osg::Vec4f);
        else
            scratch.mBand = std::size_t{ width } * 4 * sizeof(osg::Vec3f);
        if (encoding == TextureEncoding::Painted)
            scratch.mColumns = std::size_t{ width } * sizeof(PaintedAxis);
        return scratch;
    }

    ChainBuild::Scratch ChainBuild::capacity() const
    {
        return Scratch{ .mAlpha = mAlpha.capacity(),
            .mTexels = mTexels.capacity(),
            .mBand = mBand.capacity() * sizeof(osg::Vec3f),
            .mRing = mRing.capacity() * sizeof(osg::Vec4f),
            .mColumns = mColumns.capacity() * sizeof(PaintedAxis) };
    }

    void ChainBuild::letGo()
    {
        mAlpha = AlphaImage();
        letGoOf(mBand);
        letGoOf(mRing);
        letGoOf(mRingAt);
        letGoOf(mBoxed);
        letGoOf(mColumns);
        letGoOf(mTexels);
        letGoOf(mLevels);
    }

    bool ChainBuild::rebuilds(const TextureData& file)
    {
        const TextureFormat format = file.mFormat;
        const MipLevel& finest = file.mLevels.front();
        const bool normalBc5 = format == TextureFormat::Bc5Unorm && file.mEncoding == TextureEncoding::Normal;
        return std::size_t{ finest.mWidth } * finest.mHeight > 1 && buildsAt(finest) && file.levelsFit()
            && (isBc1(format) || isBc2(format) || isBc3(format) || isLoose(format) || normalBc5);
    }

    bool ChainBuild::buildsAt(const MipLevel& finest)
    {
        return std::max(finest.mWidth, finest.mHeight) <= Shaders::TEXTURE_SIDE_MOST;
    }

    void ChainBuild::build(const TextureData& file, const std::uint32_t levels)
    {
        assert(levels >= 1 && "a chain of no level");

        // **The levels read, as the readers take a description**: a painted colour's own levels, each
        // delit where it stands, and every other chain's finest alone, the levels under it what is made
        // here and never read.
        const std::uint32_t read = file.mEncoding == TextureEncoding::Painted
            ? std::min(levels, static_cast<std::uint32_t>(file.mLevels.size()))
            : 1u;
        TextureData first = file;
        first.mLevels = file.mLevels.first(read);
        first.mBytes = file.mBytes.first(file.mLevels[read - 1].mOffset + file.bytesOf(read - 1).size());
        first.mCoarser = {};
        mAlpha.build(first);

        // Each buffer grown to the size this build needs and no more, as `scratchOf` predicts it.
        const MipLevel& top = first.mLevels.front();
        mLevels.clear();
        std::size_t bytes = 0;
        for (std::uint32_t level = 0; level < levels; ++level)
        {
            const MipLevel laid{ .mOffset = static_cast<std::uint32_t>(bytes),
                .mWidth = std::max(top.mWidth >> level, 1u),
                .mHeight = std::max(top.mHeight >> level, 1u) };
            mLevels.push_back(laid);
            bytes += std::size_t{ laid.mWidth } * laid.mHeight * sTexel;
        }
        // Reserved first, so the room is what this build needs on every standard library: MSVC's `assign`
        // and `resize` grow by half again what they held, which `scratchOf` would not predict.
        assert(bytes <= std::numeric_limits<std::uint32_t>::max() && "a build past a level's 32-bit offset");
        mTexels.reserve(bytes);
        mTexels.assign(bytes, std::byte{ 0 });

        if (file.mEncoding == TextureEncoding::Normal)
        {
            buildNormals(first);
            return;
        }

        // A band of the finest level's four rows, reserved before the reader resizes it.
        mBand.reserve(std::size_t{ top.mWidth } * 4);

        // The levels read through the readers that know every format: colour a band of blocks at a
        // time, as stored, and alpha a byte a texel.
        for (std::uint32_t at = 0; at < read; ++at)
        {
            const MipLevel& level = first.mLevels[at];
            for (std::uint32_t row = 0; row < level.mHeight; row += 4)
            {
                readTexelBand(first, level, row / 4, mBand);
                for (std::uint32_t y = row; y < row + mBand.size() / level.mWidth; ++y)
                    for (std::uint32_t x = 0; x < level.mWidth; ++x)
                    {
                        const osg::Vec3f& colour = mBand[std::size_t{ y - row } * level.mWidth + x];
                        const std::size_t into = mLevels[at].texelOffset(x, y, sTexel);
                        for (int channel = 0; channel < 3; ++channel)
                            mTexels[into + static_cast<std::size_t>(channel)] = unitByte(colour[channel]);
                        mTexels[into + 3] = static_cast<std::byte>(mAlpha.at(at, x, y));
                    }
            }
        }

        if (file.mEncoding == TextureEncoding::Painted)
            delight(first);

        const DisplayCurve& curve = DisplayCurve::get();
        const bool encoded = isSrgb(file.mFormat);
        const bool coverage = isColour(file.mEncoding);
        for (std::size_t at = read; at < mLevels.size(); ++at)
        {
            const MipLevel& above = mLevels[at - 1];
            const MipLevel& level = mLevels[at];
            for (std::uint32_t y = 0; y < level.mHeight; ++y)
                for (std::uint32_t x = 0; x < level.mWidth; ++x)
                {
                    const Shaders::AxisTaps across = Shaders::axisTaps(x, above.mWidth);
                    const Shaders::AxisTaps down = Shaders::axisTaps(y, above.mHeight);

                    osg::Vec3f weighed;
                    osg::Vec3f even;
                    float painted = 0.0f;
                    for (const std::uint32_t dy : { 0u, 1u, 2u })
                        for (const std::uint32_t dx : { 0u, 1u, 2u })
                        {
                            const float weight = across.mWeights[dx] * down.mWeights[dy];
                            const std::uint32_t sx = std::min(across.mFirst + dx, above.mWidth - 1);
                            const std::uint32_t sy = std::min(down.mFirst + dy, above.mHeight - 1);
                            const std::size_t from = above.texelOffset(sx, sy, sTexel);
                            const auto stored = [&](std::size_t offset) {
                                return std::to_integer<std::uint8_t>(mTexels[from + offset]);
                            };

                            // Through the byte's table and not a `pow`, and alpha linear in every
                            // format, its byte's own share.
                            const auto channel = [&](std::size_t offset) {
                                return encoded ? curve.toLinear(stored(offset)) : stored(offset) / 255.0f;
                            };

                            const osg::Vec3f texel(channel(0), channel(1), channel(2));
                            const float alpha = stored(3) / 255.0f;
                            even += texel * weight;
                            weighed += texel * (alpha * weight);
                            painted += alpha * weight;
                        }

                    const osg::Vec3f mean = coverage && painted > 0.0f ? weighed / painted : even;
                    const std::size_t into = level.texelOffset(x, y, sTexel);
                    for (int channel = 0; channel < 3; ++channel)
                        mTexels[into + static_cast<std::size_t>(channel)]
                            = encoded ? curve.encodedByte(mean[channel]) : unitByte(mean[channel]);
                    mTexels[into + 3] = unitByte(painted);
                }
        }
    }

    void ChainBuild::delight(const TextureData& read)
    {
        assert(isSrgb(read.mFormat) && "a painted colour that is not display-encoded");

        // **The light painted in, estimated off the finest level** (`ShadingMap`), and divided out of
        // every level read at its own texels' centres, in linear light.
        // Each column placed once a level and each row once, and the four cells lerped at every texel.
        const ShadingMap map(read, mAlpha);
        const std::span<const float> painted = map.getValues();
        const DisplayCurve& curve = DisplayCurve::get();
        for (std::size_t at = 0; at < read.mLevels.size(); ++at)
        {
            const MipLevel& level = mLevels[at];
            mColumns.clear();
            mColumns.reserve(level.mWidth);
            for (std::uint32_t x = 0; x < level.mWidth; ++x)
                mColumns.push_back(paintedAxis((static_cast<float>(x) + 0.5f) / level.mWidth, clampsS(read.mWrap)));
            for (std::uint32_t y = 0; y < level.mHeight; ++y)
            {
                const PaintedAxis row
                    = paintedAxis((static_cast<float>(y) + 0.5f) / level.mHeight, clampsT(read.mWrap));
                for (std::uint32_t x = 0; x < level.mWidth; ++x)
                {
                    const float factor = paintedLight(painted, mColumns[x], row);
                    const std::size_t into = level.texelOffset(x, y, sTexel);
                    osg::Vec3f light;
                    for (int channel = 0; channel < 3; ++channel)
                        light[channel] = curve.toLinear(
                            std::to_integer<std::uint8_t>(mTexels[into + static_cast<std::size_t>(channel)]));
                    const osg::Vec3f divided = Rtx::delit(light, factor);
                    for (int channel = 0; channel < 3; ++channel)
                        mTexels[into + static_cast<std::size_t>(channel)] = curve.encodedByte(divided[channel]);
                }
            }
        }
    }

    void ChainBuild::buildNormals(const TextureData& first)
    {
        const MipLevel& top = mLevels.front();

        // **Every level's means a ring of rows**: a level's means are read only by the box of the level
        // under it, which reads three rows of them, and each row of that level is boxed as soon as the
        // rows it reads stand, so a ring of `sRingRows` holds what any box still reads, as a band reader
        // streams. Whole, the means under a 4096-square map's finest level were 67 MB.
        mRingAt.clear();
        std::size_t ringTexels = 0;
        for (const MipLevel& level : mLevels)
        {
            mRingAt.push_back(ringTexels);
            ringTexels += std::size_t{ sRingRows } * level.mWidth;
        }
        mRing.clear();
        mRing.reserve(ringTexels);
        mRing.resize(ringTexels);
        mBoxed.assign(mLevels.size(), 0);
        const auto ringRow = [&](std::size_t at, std::uint32_t y) {
            return mRing.data() + mRingAt[at] + std::size_t{ y % sRingRows } * mLevels[at].mWidth;
        };

        // A row being laid, its means in the ring and its texels in the level: found once a row and not
        // once a texel, since a level's bytes are written between, which a compiler must take to move
        // everything a row is found from.
        struct LaidRow
        {
            osg::Vec4f* mMeans = nullptr;
            std::byte* mTexels = nullptr;
        };
        const auto rowOf = [&](std::size_t at, std::uint32_t y) {
            return LaidRow{ .mMeans = ringRow(at, y),
                .mTexels = mTexels.data() + mLevels[at].texelOffset(0, y, sTexel) };
        };

        // A texel laid: its mean in the ring, with the height its byte decodes to, and in the level the
        // direction of that mean, `direction`, with the height beside it.
        const auto lay = [](const LaidRow& row, std::uint32_t x, const osg::Vec3f& mean,
                             const std::array<std::byte, 3>& direction, std::byte height) {
            row.mMeans[x] = osg::Vec4f(mean, sUnitOfByte[std::to_integer<std::size_t>(height)]);
            std::byte* const texel = row.mTexels + std::size_t{ x } * sTexel;
            std::copy(direction.begin(), direction.end(), texel);
            texel[3] = height;
        };

        // **Each level the box of the unit normals under it, stored as its direction**: the means are
        // carried unnormalised, so a level is the mean of the finest texels it covers and not a mean of
        // means each rounded to a byte and made unit again. The height beside them a plain box of the
        // bytes above. A tap is the ring's four values weighed at once, every one of the nine added in
        // its turn, a weight of nought with them.
        const auto box = [&](std::size_t at, std::uint32_t y) {
            const MipLevel& level = mLevels[at];
            const MipLevel& above = mLevels[at - 1];
            const Shaders::AxisTaps down = Shaders::axisTaps(y, above.mHeight);
            const std::array<const osg::Vec4f*, 3> rows{ ringRow(at - 1, std::min(down.mFirst, above.mHeight - 1)),
                ringRow(at - 1, std::min(down.mFirst + 1, above.mHeight - 1)),
                ringRow(at - 1, std::min(down.mFirst + 2, above.mHeight - 1)) };
            const LaidRow under = rowOf(at, y);
            for (std::uint32_t first = 0; first < level.mWidth; first += 4)
            {
                // Four texels' boxes, and their directions together: past the level's edge a mean of
                // nought, whose direction is laid nowhere.
                const std::uint32_t count = std::min(level.mWidth - first, 4u);
                std::array<osg::Vec4f, 4> boxed;
                FourNormals means;
                for (std::uint32_t lane = 0; lane < count; ++lane)
                {
                    const Shaders::AxisTaps across = Shaders::axisTaps(first + lane, above.mWidth);
                    for (const std::uint32_t dy : { 0u, 1u, 2u })
                        for (const std::uint32_t dx : { 0u, 1u, 2u })
                            boxed[lane] += rows[dy][std::min(across.mFirst + dx, above.mWidth - 1)]
                                * (across.mWeights[dx] * down.mWeights[dy]);
                    means.mX[lane] = boxed[lane].x();
                    means.mY[lane] = boxed[lane].y();
                    means.mZ[lane] = boxed[lane].z();
                }
                const std::array<std::array<std::byte, 3>, 4> directions = layDirections(means);
                for (std::uint32_t lane = 0; lane < count; ++lane)
                    lay(under, first + lane, means.at(lane), directions[lane], unitByte(boxed[lane].w()));
            }
        };

        // Each row of the level under `at` boxed once the rows of `at` its box reads stand, below `to`,
        // and each boxed row in turn boxing the level under it: the row `y` reads `2 y` to `2 y + 2`, so a
        // ring holds the two rows before the finest's band of four, and a coarser level's rows come one
        // at a time. At the end of a level every row under it is boxed, its last rows held to its edge.
        const auto boxUnder = [&](const auto& self, std::size_t at, std::uint32_t to) -> void {
            if (at + 1 >= mLevels.size())
                return;
            const std::uint32_t aboveHeight = mLevels[at].mHeight;
            std::uint32_t& next = mBoxed[at + 1];
            while (next < mLevels[at + 1].mHeight
                && (to == aboveHeight
                    || std::min(Shaders::axisTaps(next, aboveHeight).mFirst + 2, aboveHeight - 1) < to))
            {
                box(at + 1, next);
                ++next;
                self(self, at + 1, next);
            }
        };

        // **Every texel of the finest level its unit normal**, four decoded normals turned together: each
        // one's unit normal and the direction that is laid as, which is the unit normal's own direction
        // again. Its height is the file's alpha, the byte a height of that byte over 255 is laid as.
        struct Turned
        {
            osg::Vec3f mUnit;
            std::array<std::byte, 3> mDirection;
        };
        const auto turn = [](const FourNormals& decoded) {
            const FourNormals units = unitNormals(decoded);
            const std::array<std::array<std::byte, 3>, 4> directions = layDirections(units);
            std::array<Turned, 4> turned;
            for (std::size_t lane = 0; lane < 4; ++lane)
                turned[lane] = Turned{ .mUnit = units.at(lane), .mDirection = directions[lane] };
            return turned;
        };
        const auto layFinest = [&](const LaidRow& row, std::uint32_t x, std::uint32_t y, const Turned& turned) {
            lay(row, x, turned.mUnit, turned.mDirection, std::byte{ mAlpha.at(0, x, y) });
        };

        // The rows of the finest level's band `band`, the level's last band fewer.
        const auto bandOf = [&](std::uint32_t band) {
            std::array<LaidRow, 4> rows;
            for (std::uint32_t row = 0; row < bandRows(top, band); ++row)
                rows[row] = rowOf(0, band * 4 + row);
            return rows;
        };

        // Three channels decoded as `2 c − 1`, a block's palette turned and not its texels; and a map of
        // two its blue raised from the other two, as a sampler of the map would have, a block's row
        // turned at once.
        if (first.mFormat == TextureFormat::Bc5Unorm)
        {
            const TexelLayout layout = layoutOf(first.mFormat);
            for (std::uint32_t by = 0; by < (top.mHeight + 3) / 4; ++by)
            {
                const std::array<LaidRow, 4> rows = bandOf(by);
                for (std::uint32_t bx = 0; bx < (top.mWidth + 3) / 4; ++bx)
                {
                    std::array<std::uint8_t, 64> block{};
                    rgbcx::unpack_bc5(first.mBytes.data() + top.blockOffset(bx, by, layout.mBytes), block.data());
                    for (std::uint32_t y = by * 4; y < std::min(by * 4 + 4, top.mHeight); ++y)
                    {
                        FourNormals decoded;
                        for (std::size_t lane = 0; lane < 4; ++lane)
                        {
                            const std::size_t at = (std::size_t{ y - by * 4 } * 4 + lane) * sTexel;
                            const float across = block[at] / 255.0f * 2.0f - 1.0f;
                            const float along = block[at + 1] / 255.0f * 2.0f - 1.0f;
                            decoded.mX[lane] = across;
                            decoded.mY[lane] = along;
                            decoded.mZ[lane] = std::sqrt(std::max(1.0f - across * across - along * along, 0.0f));
                        }
                        const std::array<Turned, 4> turned = turn(decoded);
                        for (std::uint32_t x = bx * 4; x < std::min(bx * 4 + 4, top.mWidth); ++x)
                            layFinest(rows[y - by * 4], x, y, turned[x - bx * 4]);
                    }
                }
                boxUnder(boxUnder, 0, std::min(by * 4 + 4, top.mHeight));
            }
        }
        else
            for (std::uint32_t band = 0; band * 4 < top.mHeight; ++band)
            {
                const std::array<LaidRow, 4> rows = bandOf(band);
                walkBand(
                    first, top, band,
                    [&](const std::array<osg::Vec3f, 4>& stored) {
                        FourNormals decoded;
                        for (std::size_t lane = 0; lane < 4; ++lane)
                        {
                            decoded.mX[lane] = stored[lane].x() * 2.0f - 1.0f;
                            decoded.mY[lane] = stored[lane].y() * 2.0f - 1.0f;
                            decoded.mZ[lane] = stored[lane].z() * 2.0f - 1.0f;
                        }
                        return turn(decoded);
                    },
                    [&](std::uint32_t x, std::uint32_t row, const Turned& turned) {
                        layFinest(rows[row], x, band * 4 + row, turned);
                    });
                boxUnder(boxUnder, 0, std::min(band * 4 + 4, top.mHeight));
            }
    }

    bool ChainBuild::holdsCut(const TextureData& finest) const
    {
        if (!isBc1(finest.mFormat))
            return false;

        const MipLevel& top = mLevels.front();
        for (std::uint32_t y = 0; y < top.mHeight; ++y)
            for (std::uint32_t x = 0; x < top.mWidth; ++x)
                if (mAlpha.at(0, x, y) == 0)
                    return true;
        return false;
    }

    void ChainBuild::encodeBc7(const TextureEncoding encoding, Misc::ByteBuffer& into) const
    {
        encodeLevelsBc7(encoding, mLevels, [&](std::uint32_t at) { return getTexels(at); }, false, into);
    }

    void ChainBuild::encodeOwnBc7(const TextureData& file, Misc::ByteBuffer& into)
    {
        assert(isLoose(file.mFormat) && "own levels encoded of a file whose bytes are not its texels");
        encodeLevelsBc7(
            file.mEncoding, file.mLevels, [&](std::uint32_t at) { return file.bytesOf(at); }, isBgr(file.mFormat),
            into);
    }

    template <class TexelsOf>
    void ChainBuild::encodeLevelsBc7(const TextureEncoding encoding, const std::span<const MipLevel> levels,
        const TexelsOf& texelsOf, const bool bgr, Misc::ByteBuffer& into)
    {
        initEncoders();

        bc7enc_compress_block_params params;
        bc7enc_compress_block_params_init(&params);
        if (!isSrgb(bc7FormatOf(encoding)))
            bc7enc_compress_block_params_init_linear_weights(&params);

        // **The whole chain's room at once**: grown a level at a time, the buffer reallocated and copied
        // at each level and kept up to twice the chain.
        const TexelLayout layout = layoutOf(bc7FormatOf(encoding));
        std::size_t total = 0;
        for (const MipLevel& level : levels)
            total += layout.levelBytes(level.mWidth, level.mHeight);
        into.reserve(into.size() + total);

        for (std::uint32_t at = 0; at < levels.size(); ++at)
        {
            const MipLevel& level = levels[at];
            const std::span<const std::byte> texels = texelsOf(at);
            const std::uint32_t across = (level.mWidth + layout.mSide - 1) / layout.mSide;
            const std::uint32_t down = (level.mHeight + layout.mSide - 1) / layout.mSide;
            const std::size_t start = into.size();
            into.resize(start + layout.levelBytes(level.mWidth, level.mHeight));
            for (std::uint32_t by = 0; by < down; ++by)
                for (std::uint32_t bx = 0; bx < across; ++bx)
                {
                    std::array<std::uint8_t, 64> block = blockAt(texels, level, bx, by);
                    if (bgr)
                        for (std::size_t texel = 0; texel < 16; ++texel)
                            std::swap(block[texel * sTexel], block[texel * sTexel + 2]);

                    // **A normal map's block in one mode**, as DirectXTex's `TEX_COMPRESS_BC7_QUICK` narrows by mode: 5
                    // where it holds a height, 6 where it holds none. Five times the full search's speed
                    // (`docs/architecture.md`, "A normal map is whole in BC7, in one encoding"). bc7enc tells the
                    // kinds apart by the same test and asserts a mode it can take.
                    if (encoding == TextureEncoding::Normal)
                    {
                        bool height = false;
                        for (std::size_t texel = 0; texel < 16; ++texel)
                            height = height || block[texel * sTexel + 3] < 255;
                        params.m_mode_mask = height ? 1u << 5 : 1u << 6;
                    }
                    bc7enc_compress_block(
                        into.data() + start + (std::size_t{ by } * across + bx) * layout.mBytes, block.data(), &params);
                }
        }
    }

    std::span<const std::byte> ChainBuild::getTexels(std::uint32_t level) const
    {
        const MipLevel& at = mLevels[level];
        return std::span(mTexels).subspan(at.mOffset, std::size_t{ at.mWidth } * at.mHeight * sTexel);
    }

    void ChainBuild::encode(const TextureData& file, const std::uint32_t from, Misc::ByteBuffer& into) const
    {
        assert(mLevels.size() >= file.mLevels.size() && "a chain encoded for a file it was not built of");
        assert(!isLoose(file.mFormat) && "a loose file's chain is made in BC7");
        initEncoders();

        // The room of every level encoded at once, for `encodeBc7`'s reason.
        const TextureFormat format = file.mFormat;
        const std::size_t blockBytes = isBc1(format) ? 8 : 16;
        const auto levelBytes = [&](const MipLevel& level) {
            return std::size_t{ (level.mWidth + 3) / 4 } * ((level.mHeight + 3) / 4) * blockBytes;
        };
        std::size_t total = 0;
        for (std::uint32_t at = from; at < getLevelCount(); ++at)
            total += levelBytes(mLevels[at]);
        into.reserve(into.size() + total);

        for (std::uint32_t at = from; at < getLevelCount(); ++at)
        {
            const MipLevel& level = mLevels[at];
            const std::span<const std::byte> texels = getTexels(at);
            const std::uint32_t across = (level.mWidth + 3) / 4;
            const std::size_t start = into.size();
            into.resize(start + levelBytes(level));
            const std::uint32_t down = (level.mHeight + 3) / 4;
            for (std::uint32_t by = 0; by < down; ++by)
                for (std::uint32_t bx = 0; bx < across; ++bx)
                {
                    const std::array<std::uint8_t, 64> block = blockAt(texels, level, bx, by);
                    std::byte* const target = into.data() + start + (std::size_t{ by } * across + bx) * blockBytes;

                    if (isBc1(format))
                        rgbcx::encode_bc1(sEncodeLevel, target, block.data(), true, false);
                    else if (isBc3(format))
                        rgbcx::encode_bc3(sEncodeLevel, target, block.data());
                    else
                    {
                        // BC2's alpha is sixteen nibbles, texel by texel, the low one first; its
                        // colour half a BC1 block that is always of four colours. Each byte written
                        // whole: the output grows with nothing written into it (`Misc::ByteBuffer`).
                        const auto nibbleOf
                            = [&](std::size_t texel) { return (block[texel * sTexel + 3] * 15u + 127u) / 255u; };
                        for (std::size_t pair = 0; pair < 8; ++pair)
                            target[pair] = static_cast<std::byte>(nibbleOf(2 * pair) | nibbleOf(2 * pair + 1) << 4);
                        rgbcx::encode_bc1(sEncodeLevel, target + 8, block.data(), false, false);
                    }
                }
        }
    }
}
