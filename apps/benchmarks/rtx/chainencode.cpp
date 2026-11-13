#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <benchmark/benchmark.h>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture> // The S3TC formats, which Windows's gl.h lacks.
#include <osg/ref_ptr>
#include <osgDB/Options>
#include <osgDB/ReaderWriter>
#include <osgDB/Registry>
#include <rgbcx/bc7decomp.h>
#include <rgbcx/rgbcx.h>

#include <components/files/conversion.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/misc/hashstate.hpp>
#include <components/misc/result.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/rtx/image/alphaimage.hpp>
#include <components/rtx/image/blockdecode.hpp>
#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>

// What the loading screen's preparation spends most of its threads on: a painted colour's chain made again in
// BC1, every level delit and encoded by rgbcx, a normal map's made in BC7 by bc7enc, and in every mode of BC7 the
// painted colour's that BC1 cannot hold (`docs/research/2026-11-13-preparation-cpu-by-codec.md`).
//
// **Over a corpus of each kind, each file taken as the chain pass takes it**: the files of the folder
// `OPENMW_RTX_BENCH_TEXTURES` names, sampled across it up to its kind's most texels, or pictures made here where it
// names none, which is what CTest's dry run and a box with no content read. A number is quoted off a folder of the
// content's own textures, whose mix of blocks the made pictures do not have.
//
// **The label is the corpus and a digest of every byte the pass made of it, and the counters what the encoding
// lost of it**, so a change to an encoder that moves a byte shows beside the time it bought and the error it cost.

namespace Rtx
{
    namespace
    {
        /// The most files read to fill a corpus from a folder.
        constexpr std::size_t sMostFiles = 512;

        /// What a corpus's files are read as, which is the encoder their chains go through.
        enum class ChainKind : std::uint8_t
        {
            /// A BC1 colour read as painted: its whole chain delit and made again in BC1, by rgbcx.
            PaintedBc1,

            /// A normal map: its whole chain made in BC7, a block in one mode, by bc7enc.
            NormalBc7,

            /// A colour read as painted that BC1 cannot hold again, a cut in it, or loose, or small with no
            /// chain: its whole chain delit and made in BC7, a block in the best of every mode bc7enc has.
            PaintedBc7,
        };

        /// What tells a kind from the others.
        struct KindTraits
        {
            TextureEncoding mEncoding;

            /// The form the pass makes its chains in.
            ChainForm mForm;

            /// The most texels of finest levels its corpus holds, read from a folder: some seconds an
            /// iteration at its encoder's rate, a block of every mode's being ten of the others'.
            std::size_t mMostTexels;

            /// Its three benchmarks: its build, its encoder and its pass.
            const char* mBuild;
            const char* mEncode;
            const char* mMake;
        };

        constexpr KindTraits traitsOf(ChainKind kind)
        {
            switch (kind)
            {
                case ChainKind::PaintedBc1:
                    return { TextureEncoding::Painted, ChainForm::Own, std::size_t{ 32 } << 20, "buildPaintedChain",
                        "encodePaintedBc1", "makePaintedChain" };
                case ChainKind::NormalBc7:
                    return { TextureEncoding::Normal, ChainForm::Bc7, std::size_t{ 32 } << 20, "buildNormalChain",
                        "encodeNormalBc7", "makeNormalChain" };
                case ChainKind::PaintedBc7:
                    break;
            }
            return { TextureEncoding::Painted, ChainForm::Bc7, std::size_t{ 4 } << 20, "buildPaintedBc7Chain",
                "encodePaintedBc7", "makePaintedBc7Chain" };
        }

        constexpr TextureEncoding encodingOf(ChainKind kind)
        {
            return traitsOf(kind).mEncoding;
        }

        /// Whether a file of this name goes to `kind`'s corpus, by the patterns the settings name a companion
        /// map with where a player has set no other: a normal map (`_n`, `_nh`) to the normals', a specular
        /// one (`_spec`) to none, and every other file to the colours'.
        bool isNamedAs(ChainKind kind, const std::filesystem::path& path)
        {
            const std::string stem = Misc::StringUtils::lowerCase(Files::pathToUnicodeString(path.stem()));
            const bool normal = stem.ends_with("_n") || stem.ends_with("_nh");
            return kind == ChainKind::NormalBc7 ? normal : !normal && !stem.ends_with("_spec");
        }

        /// Whether the pass could make a painted `file` in BC7: one loose, or small with no chain, or a BC1
        /// file with a hole in its finest level. So a file it would make in its own format is not encoded to
        /// be left out of the corpus; the pass still says which of these it does make so.
        bool mayBePaintedInBc7(const TextureData& file)
        {
            const MipLevel& finest = file.mLevels.front();
            if (!layoutOf(file.mFormat).isBlocked()
                || (file.mLevels.size() == 1 && std::max(finest.mWidth, finest.mHeight) <= sLargestBc7ChainSide))
                return true;
            if (!isBc1(file.mFormat))
                return false;

            const AlphaImage alpha(file);
            for (std::uint32_t y = 0; y < finest.mHeight; ++y)
                for (std::uint32_t x = 0; x < finest.mWidth; ++x)
                    if (alpha.at(0, x, y) == 0)
                        return true;
            return false;
        }

        /// A file read as its kind, with the chain the pass made of it and the build that stands behind that
        /// chain. Pinned: its description spans the image and the levels it holds.
        class CorpusChain
        {
        public:
            /// Describes `image` and runs the chain pass over it: the pass decides how a file's chain is made,
            /// so a corpus holds what a preparation would encode its kind's way and nothing it would not. A
            /// painted colour in a format its kind's corpus cannot hold is left unread, so none is encoded to
            /// be left out.
            CorpusChain(osg::ref_ptr<const osg::Image> image, ChainKind kind)
                : mKind(kind)
                , mImage(std::move(image))
            {
                const Result<TextureData, std::string> described
                    = describeImage(*mImage, encodingOf(mKind), mLevels, mTexels);
                if (!described.isOk() || (mKind == ChainKind::PaintedBc1 && !isBc1(described.value().mFormat))
                    || (mKind == ChainKind::PaintedBc7 && !mayBePaintedInBc7(described.value())))
                    return;

                mFile = described.value();
                ChainPass pass(mBuilt);
                pass.run(ChainPass::Input{ .mImage = *mImage, .mEncoding = encodingOf(mKind) }, mMade);

                if (isMadeAsItsKind())
                {
                    const std::span<const std::byte> finest = mBuilt.getTexels(0);
                    for (std::size_t texel = 0; texel < finest.size() / 4 && mChannels == 3; ++texel)
                        if (finest[texel * 4 + 3] != std::byte{ 255 })
                            mChannels = 4;
                }
            }

            CorpusChain(const CorpusChain&) = delete;
            CorpusChain& operator=(const CorpusChain&) = delete;
            CorpusChain(CorpusChain&&) = delete;
            CorpusChain& operator=(CorpusChain&&) = delete;
            ~CorpusChain() = default;

            /// Whether the pass made the file's whole chain its kind's way: a painted colour's in BC1, which
            /// has no cut and is not small and chainless, and a normal map's or the other painted colours' in
            /// BC7.
            bool isMadeAsItsKind() const { return mMade.mForm == traitsOf(mKind).mForm; }

            const osg::Image& getImage() const { return *mImage; }

            /// The chain the pass made.
            std::span<const std::byte> getMade() const { return mMade.mBytes; }

            /// The texels of the levels from `first` down.
            std::size_t countTexels(std::uint32_t first = 0) const
            {
                std::size_t texels = 0;
                for (std::uint32_t level = first; level < mBuilt.getLevelCount(); ++level)
                    texels += std::size_t{ mBuilt.getLevel(level).mWidth } * mBuilt.getLevel(level).mHeight;
                return texels;
            }

            /// How many channels of a texel the chain holds, which its error is measured over: three, and its
            /// alpha with them where its finest level holds one under 255, a normal map's height or a colour's
            /// cut, so a chain with none does not count a channel that nothing can be lost of.
            std::size_t countChannels() const { return mChannels; }

            /// The squares of what the made chain's channels decode to less the built texels they were encoded
            /// from, summed over every level and each of `countChannels`: what the encoding lost.
            std::uint64_t measureSquaredError() const
            {
                assert(isMadeAsItsKind() && "an error measured of a chain the pass made another way");
                const TextureFormat made
                    = ChainPass::madeLayout({ .mImage = *mImage, .mEncoding = encodingOf(mKind) }, mMade.mForm)
                          ->mFormat;
                std::uint64_t squares = 0;
                std::vector<std::uint8_t> decoded;
                std::size_t at = 0;
                for (std::uint32_t level = 0; level < mBuilt.getLevelCount(); ++level)
                {
                    const MipLevel& laid = mBuilt.getLevel(level);
                    const std::size_t bytes = layoutOf(made).levelBytes(laid.mWidth, laid.mHeight);
                    decoded.resize(std::size_t{ laid.mWidth } * laid.mHeight * 4);
                    decodeLevel(getMade().subspan(at, bytes), laid, decoded);
                    at += bytes;

                    const std::span<const std::byte> built = mBuilt.getTexels(level);
                    for (std::size_t texel = 0; texel < decoded.size() / 4; ++texel)
                        for (std::size_t channel = 0; channel < mChannels; ++channel)
                        {
                            const int lost = int{ decoded[texel * 4 + channel] }
                                - std::to_integer<int>(built[texel * 4 + channel]);
                            squares += static_cast<std::uint64_t>(lost * lost);
                        }
                }
                assert(at == getMade().size() && "a made chain laid as the built levels are not");
                return squares;
            }

            /// Builds the chain again in `into`, as the pass built it: its levels read and boxed, a painted
            /// colour's delit.
            void build(ChainBuild& into) const
            {
                assert(isMadeAsItsKind() && "a chain built that the pass made another way");
                into.build(*mFile, mBuilt.getLevelCount());
            }

            /// Appends the built chain to `into` as the pass encodes its kind's: in the file's own format from
            /// its finest level (`ChainBuild::encode`), or in BC7 (`ChainBuild::encodeBc7`).
            void encode(Misc::ByteBuffer& into) const
            {
                assert(isMadeAsItsKind() && "a chain encoded that the pass made another way");
                if (mMade.mForm == ChainForm::Own)
                    mBuilt.encode(*mFile, 0, into);
                else
                    mBuilt.encodeBc7(encodingOf(mKind), into);
            }

        private:
            /// One made level's blocks as RGBA8, rows tight, the blocks past its edge cut off.
            void decodeLevel(
                std::span<const std::byte> blocks, const MipLevel& laid, std::span<std::uint8_t> into) const
            {
                if (mMade.mForm == ChainForm::Own)
                {
                    [[maybe_unused]] const bool read
                        = decodeBlocks(mFile->mFormat, blocks, laid.mWidth, laid.mHeight, into);
                    assert(read && "a made chain that does not decode as the file's format");
                    return;
                }

                const std::uint32_t across = (laid.mWidth + 3) / 4;
                for (std::size_t block = 0; block < blocks.size() / 16; ++block)
                {
                    std::array<bc7decomp::color_rgba, 16> sixteen{};
                    [[maybe_unused]] const bool read
                        = bc7decomp::unpack_bc7(blocks.data() + block * 16, sixteen.data());
                    assert(read && "a made block that is no BC7 block");
                    const auto bx = static_cast<std::uint32_t>(block % across) * 4;
                    const auto by = static_cast<std::uint32_t>(block / across) * 4;
                    for (std::uint32_t y = by; y < std::min(by + 4, laid.mHeight); ++y)
                        for (std::uint32_t x = bx; x < std::min(bx + 4, laid.mWidth); ++x)
                            std::memcpy(into.data() + (std::size_t{ y } * laid.mWidth + x) * 4,
                                &sixteen[std::size_t{ y - by } * 4 + (x - bx)], 4);
                }
            }

            ChainKind mKind;
            osg::ref_ptr<const osg::Image> mImage;
            std::vector<MipLevel> mLevels;
            std::vector<std::byte> mTexels;
            std::optional<TextureData> mFile;
            ChainBuild mBuilt;
            MadeChain mMade;
            std::size_t mChannels = 3;
        };

        /// The files of one kind a run encodes, and what names them in its label.
        struct Corpus
        {
            ChainKind mKind;
            std::vector<std::unique_ptr<CorpusChain>> mChains{};

            /// The texels of every level the pass encodes, and of the finest levels alone.
            std::size_t mTexels = 0;
            std::size_t mFinestTexels = 0;

            /// What the encoding lost of them (`CorpusChain::measureSquaredError`), and how many values of a
            /// channel that is over: each chain's texels times its channels.
            std::uint64_t mSquaredError = 0;
            std::size_t mSamples = 0;

            /// Every byte the pass made of the files, in their order.
            Misc::HashState mMade{};

            /// Adds `image` where the pass makes its whole chain the corpus's way, and leaves it out where not.
            void add(osg::ref_ptr<const osg::Image> image)
            {
                auto chain = std::make_unique<CorpusChain>(std::move(image), mKind);
                if (!chain->isMadeAsItsKind())
                    return;

                mTexels += chain->countTexels();
                mFinestTexels += chain->countTexels() - chain->countTexels(1);
                mSquaredError += chain->measureSquaredError();
                mSamples += chain->countTexels() * chain->countChannels();
                mMade.add(chain->getMade());
                mChains.push_back(std::move(chain));
            }

            std::string describe() const
            {
                return std::format("{} files, {:.2f} Mtexel, made {:016x}{:016x}", mChains.size(),
                    static_cast<double>(mTexels) / 1.0e6, mMade.getWords()[0], mMade.getWords()[1]);
            }

            /// Writes the corpus beside a benchmark's time, and what its encoding lost: the root of the mean
            /// square a channel, in bytes, and the same as a peak signal-to-noise ratio in decibels.
            void report(benchmark::State& state) const
            {
                const double rmse = std::sqrt(static_cast<double>(mSquaredError) / static_cast<double>(mSamples));
                state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(mTexels));
                state.counters["rmse"] = rmse;
                state.counters["psnr"] = 20.0 * std::log10(255.0 / rmse);
                state.SetLabel(describe());
            }
        };

        /// The `.dds` files under `folder`, their paths in order. Throws `std::filesystem::filesystem_error`
        /// where the folder cannot be walked.
        std::vector<std::filesystem::path> listTextures(const std::filesystem::path& folder)
        {
            std::vector<std::filesystem::path> paths;
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(
                     folder, std::filesystem::directory_options::skip_permission_denied))
                if (entry.is_regular_file()
                    && Misc::StringUtils::lowerCase(Files::pathToUnicodeString(entry.path().extension())) == ".dds")
                    paths.push_back(entry.path());
            std::ranges::sort(paths);
            return paths;
        }

        /// The files of `paths` named as `kind`'s (`isNamedAs`), `sMostFiles` of them at most, spaced evenly
        /// over their order and read as the game's loader reads one, until the corpus holds its kind's most
        /// texels.
        Corpus readCorpus(std::span<const std::filesystem::path> paths, ChainKind kind)
        {
            std::vector<const std::filesystem::path*> named;
            for (const std::filesystem::path& path : paths)
                if (isNamedAs(kind, path))
                    named.push_back(&path);

            osgDB::ReaderWriter* const reader = osgDB::Registry::instance()->getReaderWriterForExtension("dds");
            assert(reader != nullptr && "the build links the reader of .dds files into every program");

            // The loader's own options (`Resource::ImageManager`), so a file's format is read as the game reads it.
            const osg::ref_ptr<osgDB::Options> options = new osgDB::Options("dds_dxt1_detect_rgba");

            // **The sampled paths visited a golden step apart**, so a corpus that fills after a few files holds
            // files from across the folder and not its first names: the step is the count's golden section,
            // moved up to share no factor with it, so the walk meets every sampled path once.
            Corpus corpus{ .mKind = kind };
            const std::size_t sampled = std::min(named.size(), sMostFiles);
            auto step = std::max<std::size_t>(static_cast<std::size_t>(static_cast<double>(sampled) * 0.6180339887), 1);
            while (std::gcd(step, sampled) != 1)
                ++step;
            for (std::size_t visit = 0; visit < sampled && corpus.mFinestTexels < traitsOf(kind).mMostTexels; ++visit)
            {
                std::ifstream stream(*named[visit * step % sampled * named.size() / sampled], std::ios::binary);
                // Not `const`: OpenSceneGraph hands a result's image out through a member that is not.
                osgDB::ReaderWriter::ReadResult read = reader->readImage(stream, options.get());
                if (read.validImage())
                    corpus.add(read.getImage());
            }
            return corpus;
        }

        /// A lattice point's value from nought to one, by an integer hash, so a made picture is the same on
        /// every compiler's library.
        float latticeAt(std::uint32_t x, std::uint32_t y, std::uint32_t seed)
        {
            std::uint32_t hash = x * 0x9E3779B1u ^ y * 0x85EBCA77u ^ seed * 0xC2B2AE3Du;
            hash ^= hash >> 15;
            hash *= 0x2C1B3C6Du;
            hash ^= hash >> 12;
            hash *= 0x297A2D39u;
            hash ^= hash >> 15;
            return static_cast<float>(hash >> 8) * (1.0f / 16777216.0f);
        }

        /// Value noise at `x`, `y`: the lattice's values eased between its points.
        float noiseAt(float x, float y, std::uint32_t seed)
        {
            const float floorX = std::floor(x);
            const float floorY = std::floor(y);
            const auto cellX = static_cast<std::uint32_t>(static_cast<std::int32_t>(floorX));
            const auto cellY = static_cast<std::uint32_t>(static_cast<std::int32_t>(floorY));
            const auto ease = [](float t) { return t * t * (3.0f - 2.0f * t); };
            const float across = ease(x - floorX);
            const float down = ease(y - floorY);
            const float top = std::lerp(latticeAt(cellX, cellY, seed), latticeAt(cellX + 1, cellY, seed), across);
            const float bottom
                = std::lerp(latticeAt(cellX, cellY + 1, seed), latticeAt(cellX + 1, cellY + 1, seed), across);
            return std::lerp(top, bottom, down);
        }

        /// Four octaves of `noiseAt`, each half the one before at twice its frequency, from nought to one.
        float cloudAt(float x, float y, std::uint32_t seed)
        {
            float sum = 0.0f;
            float weight = 0.5f;
            for (std::uint32_t octave = 0; octave < 4; ++octave)
            {
                sum += weight * noiseAt(x, y, seed + octave);
                x *= 2.0f;
                y *= 2.0f;
                weight *= 0.5f;
            }
            return sum / 0.9375f;
        }

        /// What a made picture is painted of: two colours it mottles between, display-encoded.
        struct Palette
        {
            std::array<float, 3> mFirst;
            std::array<float, 3> mSecond;
        };

        /// A picture as a vanilla texture is one, `width` by `height` of fours: two colours mottled, a
        /// light painted in over a few texels a cycle, and grain; its chain boxed to four texels a side and
        /// every level in BC1, as a loader hands a `.dds` over.
        osg::ref_ptr<const osg::Image> makePicture(
            std::uint32_t width, std::uint32_t height, const Palette& palette, std::uint32_t seed)
        {
            std::vector<std::uint8_t> texels(std::size_t{ width } * height * 4);
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const float u = static_cast<float>(x) / 256.0f;
                    const float v = static_cast<float>(y) / 256.0f;
                    const float mottle = cloudAt(u * 6.0f, v * 6.0f, seed);
                    const float light = 0.55f + 0.6f * cloudAt(u * 1.5f, v * 1.5f, seed + 16);
                    const float grain = 0.12f * (cloudAt(u * 48.0f, v * 48.0f, seed + 32) - 0.5f);
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        const float colour
                            = std::lerp(palette.mFirst[channel], palette.mSecond[channel], mottle) * light + grain;
                        texels[(std::size_t{ y } * width + x) * 4 + channel]
                            = static_cast<std::uint8_t>(std::lround(std::clamp(colour, 0.0f, 1.0f) * 255.0f));
                    }
                    texels[(std::size_t{ y } * width + x) * 4 + 3] = 255;
                }

            std::vector<std::uint8_t> file;
            osg::Image::MipmapDataType offsets;
            for (std::uint32_t across = width, down = height;; across /= 2, down /= 2)
            {
                if (!file.empty())
                    offsets.push_back(static_cast<unsigned int>(file.size()));
                for (std::uint32_t by = 0; by < down / 4; ++by)
                    for (std::uint32_t bx = 0; bx < across / 4; ++bx)
                    {
                        std::array<std::uint8_t, 64> block{};
                        for (std::uint32_t y = 0; y < 4; ++y)
                            std::memcpy(block.data() + std::size_t{ y } * 16,
                                texels.data() + (std::size_t{ by * 4 + y } * across + bx * 4) * 4, 16);
                        std::array<std::uint8_t, 8> encoded{};
                        rgbcx::encode_bc1(rgbcx::MIN_LEVEL, encoded.data(), block.data(), false, false);
                        file.insert(file.end(), encoded.begin(), encoded.end());
                    }
                if (std::min(across, down) <= 4)
                    break;

                // The level under it, each texel the mean of the four above, in place: a texel is written
                // after the four it reads.
                for (std::uint32_t y = 0; y < down / 2; ++y)
                    for (std::uint32_t x = 0; x < across / 2; ++x)
                        for (std::size_t channel = 0; channel < 4; ++channel)
                        {
                            const auto above = [&](std::uint32_t dx, std::uint32_t dy) {
                                return std::uint32_t{
                                    texels[(std::size_t{ 2 * y + dy } * across + 2 * x + dx) * 4 + channel]
                                };
                            };
                            texels[(std::size_t{ y } * (across / 2) + x) * 4 + channel] = static_cast<std::uint8_t>(
                                (above(0, 0) + above(1, 0) + above(0, 1) + above(1, 1) + 2) / 4);
                        }
            }

            osg::ref_ptr<osg::Image> image = new osg::Image;
            auto* const data = new unsigned char[file.size()];
            std::memcpy(data, file.data(), file.size());
            image->setImage(static_cast<int>(width), static_cast<int>(height), 1, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
                GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_UNSIGNED_BYTE, data, osg::Image::USE_NEW_DELETE);
            image->setMipmapLevels(offsets);
            return image;
        }

        /// A loose picture made here, `side` square, one level, each texel what `texelAt(x, y)` says of its
        /// four channels from nought to one.
        template <class TexelAt>
        osg::ref_ptr<const osg::Image> makeLoose(std::uint32_t side, const TexelAt& texelAt)
        {
            auto* const data = new unsigned char[std::size_t{ side } * side * 4];
            for (std::uint32_t y = 0; y < side; ++y)
                for (std::uint32_t x = 0; x < side; ++x)
                {
                    const std::array<float, 4> texel = texelAt(x, y);
                    for (std::size_t channel = 0; channel < 4; ++channel)
                        data[(std::size_t{ y } * side + x) * 4 + channel]
                            = static_cast<unsigned char>(std::lround(std::clamp(texel[channel], 0.0f, 1.0f) * 255.0f));
                }

            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->setImage(static_cast<int>(side), static_cast<int>(side), 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,
                data, osg::Image::USE_NEW_DELETE);
            return image;
        }

        /// A normal map made here: the slopes of a rolling height, with that height in its alpha where
        /// `height` asks and opaque where not, so the one's blocks are encoded in the mode that holds a
        /// height and the other's in the mode that holds none.
        osg::ref_ptr<const osg::Image> makeNormalMap(std::uint32_t side, bool height, std::uint32_t seed)
        {
            const auto heightAt = [&](std::uint32_t x, std::uint32_t y) {
                return cloudAt(static_cast<float>(x % side) / 32.0f, static_cast<float>(y % side) / 32.0f, seed);
            };
            return makeLoose(side, [&](std::uint32_t x, std::uint32_t y) {
                const float across = (heightAt(x + 1, y) - heightAt(x + side - 1, y)) * 8.0f;
                const float down = (heightAt(x, y + 1) - heightAt(x, y + side - 1)) * 8.0f;
                const float length = std::sqrt(across * across + down * down + 1.0f);
                return std::array<float, 4>{ -across / length * 0.5f + 0.5f, -down / length * 0.5f + 0.5f,
                    1.0f / length * 0.5f + 0.5f, height ? heightAt(x, y) : 1.0f };
            });
        }

        /// A leafy colour made here, loose: two greens mottled under a painted light, cut through where a
        /// cloud runs thin, soft at the cut's edge, so its blocks are opaque, cut and between.
        osg::ref_ptr<const osg::Image> makeCutColour(std::uint32_t side, std::uint32_t seed)
        {
            return makeLoose(side, [&](std::uint32_t x, std::uint32_t y) {
                const float u = static_cast<float>(x) / 64.0f;
                const float v = static_cast<float>(y) / 64.0f;
                const float mottle = cloudAt(u * 6.0f, v * 6.0f, seed);
                const float light = 0.55f + 0.6f * cloudAt(u * 1.5f, v * 1.5f, seed + 16);
                const float cover = (cloudAt(u * 3.0f, v * 3.0f, seed + 32) - 0.42f) * 12.0f;
                return std::array<float, 4>{ std::lerp(0.20f, 0.42f, mottle) * light,
                    std::lerp(0.38f, 0.30f, mottle) * light, std::lerp(0.12f, 0.20f, mottle) * light, cover };
            });
        }

        /// The pictures made here for a kind: stone, wood and plaster, a square and two oblongs, for the
        /// colours BC1 holds; two normal maps, one with a height and one without; and two cut colours.
        Corpus makeCorpus(ChainKind kind)
        {
            Corpus corpus{ .mKind = kind };
            switch (kind)
            {
                case ChainKind::NormalBc7:
                    corpus.add(makeNormalMap(256, true, 4));
                    corpus.add(makeNormalMap(256, false, 5));
                    break;
                case ChainKind::PaintedBc7:
                    corpus.add(makeCutColour(128, 6));
                    corpus.add(makeCutColour(64, 7));
                    break;
                case ChainKind::PaintedBc1:
                    // rgbcx's tables, for the pictures' own blocks: `ChainBuild` makes the same ones again by
                    // its own flag, on this thread.
                    rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
                    corpus.add(makePicture(512, 512, Palette{ { 0.42f, 0.38f, 0.33f }, { 0.25f, 0.27f, 0.22f } }, 1));
                    corpus.add(makePicture(256, 512, Palette{ { 0.45f, 0.30f, 0.18f }, { 0.22f, 0.14f, 0.08f } }, 2));
                    corpus.add(makePicture(256, 128, Palette{ { 0.75f, 0.70f, 0.60f }, { 0.55f, 0.50f, 0.45f } }, 3));
                    break;
            }
            return corpus;
        }

        /// The build alone: every file's levels read and boxed, a painted colour's delit, with no block encoded.
        void buildChains(benchmark::State& state, const Corpus& corpus)
        {
            ChainBuild build;
            for ([[maybe_unused]] auto _ : state)
                for (const std::unique_ptr<CorpusChain>& chain : corpus.mChains)
                {
                    chain->build(build);
                    benchmark::DoNotOptimize(build.getTexels(0).data());
                }
            corpus.report(state);
        }

        /// The encoder alone: every built chain of the corpus encoded, which is the chain the pass made of it,
        /// byte for byte.
        void encodeChains(benchmark::State& state, const Corpus& corpus)
        {
            Misc::ByteBuffer made;
            for ([[maybe_unused]] auto _ : state)
                for (const std::unique_ptr<CorpusChain>& chain : corpus.mChains)
                {
                    made.clear();
                    chain->encode(made);
                    benchmark::DoNotOptimize(made.data());
                    assert(std::ranges::equal(made, chain->getMade()) && "the encoder alone made another chain");
                }
            corpus.report(state);
        }

        /// What a preparation thread runs for a file: the pass whole, its read, its build and its encoding.
        void makeChains(benchmark::State& state, const Corpus& corpus)
        {
            ChainBuild build;
            ChainPass pass(build);
            MadeChain made;
            for ([[maybe_unused]] auto _ : state)
                for (const std::unique_ptr<CorpusChain>& chain : corpus.mChains)
                {
                    pass.run(
                        ChainPass::Input{ .mImage = chain->getImage(), .mEncoding = encodingOf(corpus.mKind) }, made);
                    benchmark::DoNotOptimize(made.mBytes.data());
                }
            corpus.report(state);
        }

        /// Registers a corpus's three benchmarks, which hold it by reference: the caller keeps it past the run.
        void registerChains(const Corpus& corpus)
        {
            const KindTraits traits = traitsOf(corpus.mKind);
            benchmark::RegisterBenchmark(traits.mBuild, [&corpus](benchmark::State& state) {
                buildChains(state, corpus);
            })->Unit(benchmark::kMillisecond);
            benchmark::RegisterBenchmark(traits.mEncode, [&corpus](benchmark::State& state) {
                encodeChains(state, corpus);
            })->Unit(benchmark::kMillisecond);
            benchmark::RegisterBenchmark(traits.mMake, [&corpus](benchmark::State& state) {
                makeChains(state, corpus);
            })->Unit(benchmark::kMillisecond);
        }

        /// The run's corpora, a kind each and in the kinds' order: the folder's where
        /// `OPENMW_RTX_BENCH_TEXTURES` names one, the made pictures where it names none. And their benchmarks
        /// run, a kind with no file having none; false where no kind has a file.
        bool runChains()
        {
            const char* const folder = std::getenv("OPENMW_RTX_BENCH_TEXTURES");
            const std::vector<std::filesystem::path> paths
                = folder != nullptr ? listTextures(folder) : std::vector<std::filesystem::path>();
            const auto stand
                = [&](ChainKind kind) { return folder != nullptr ? readCorpus(paths, kind) : makeCorpus(kind); };
            const std::array<Corpus, 3> corpora{ stand(ChainKind::PaintedBc1), stand(ChainKind::NormalBc7),
                stand(ChainKind::PaintedBc7) };

            bool any = false;
            for (const Corpus& corpus : corpora)
                if (!corpus.mChains.empty())
                {
                    registerChains(corpus);
                    any = true;
                }
            if (any)
                benchmark::RunSpecifiedBenchmarks();
            return any;
        }
    }
}

int main(int argc, char** argv)
{
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;

    try
    {
        if (!Rtx::runChains())
        {
            std::fputs(
                "no file under OPENMW_RTX_BENCH_TEXTURES makes a painted chain in BC1 or BC7 or a normal map's in "
                "BC7\n",
                stderr);
            return 1;
        }
        benchmark::Shutdown();
    }
    catch (const std::filesystem::filesystem_error& error)
    {
        std::fprintf(stderr, "OPENMW_RTX_BENCH_TEXTURES: %s\n", error.what());
        return 1;
    }
    return 0;
}
