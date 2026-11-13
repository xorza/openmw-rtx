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

#include <osg/Image>
#include <osg/Texture> // The S3TC formats, which Windows's gl.h lacks.
#include <osg/ref_ptr>
#include <osgDB/Options>
#include <osgDB/ReaderWriter>
#include <osgDB/Registry>
#include <rgbcx/rgbcx.h>

#include <components/files/conversion.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/misc/hashstate.hpp>
#include <components/misc/result.hpp>
#include <components/misc/strings/lower.hpp>
#include <components/rtx/image/blockdecode.hpp>
#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>

// What the loading screen's preparation spends most of its threads on: a painted colour's chain made again in
// BC1, every level delit and encoded by rgbcx (`docs/research/2026-11-13-preparation-cpu-by-codec.md`).
//
// **Over a corpus of BC1 files, each taken as the chain pass takes it**: the files of the folder
// `OPENMW_RTX_BENCH_TEXTURES` names, sampled across it up to `sMostTexels`, or three pictures made here where it
// names none, which is what CTest's dry run and a box with no content read. A number is quoted off a folder of
// the content's own textures, whose mix of blocks the made pictures do not have.
//
// **The label is the corpus and a digest of every byte the pass made of it, and the counters what the encoding
// lost of it**, so a change to the encoder that moves a byte shows beside the time it bought and the error it cost.

namespace Rtx
{
    namespace
    {
        /// The most texels of finest levels a corpus read from a folder holds, some seconds an iteration at
        /// the build's search, and the most files read to find them.
        constexpr std::size_t sMostTexels = std::size_t{ 32 } << 20;
        constexpr std::size_t sMostFiles = 512;

        /// A file read as a painted colour, with the chain the pass made of it and the build that stands
        /// behind that chain. Pinned: its description spans the image and the levels it holds.
        class PaintedChain
        {
        public:
            /// Describes `image` and, where it is a BC1 file, runs the chain pass over it: the pass decides
            /// how a file's chain is made, so a corpus holds what a preparation would encode in BC1 and
            /// nothing it would not, and no other format is encoded here to be left out.
            explicit PaintedChain(osg::ref_ptr<const osg::Image> image)
                : mImage(std::move(image))
            {
                const Result<TextureData, std::string> described
                    = describeImage(*mImage, TextureEncoding::Painted, mLevels, mTexels);
                if (!described.isOk() || !isBc1(described.value().mFormat))
                    return;

                mFile = described.value();
                ChainPass pass(mBuilt);
                pass.run(ChainPass::Input{ .mImage = *mImage, .mEncoding = TextureEncoding::Painted }, mMade);
            }

            PaintedChain(const PaintedChain&) = delete;
            PaintedChain& operator=(const PaintedChain&) = delete;
            PaintedChain(PaintedChain&&) = delete;
            PaintedChain& operator=(PaintedChain&&) = delete;
            ~PaintedChain() = default;

            /// Whether the pass made the file's whole chain in BC1: a painted colour's with no cut, which is
            /// not small and chainless.
            bool isMadeInBc1() const { return mMade.mForm == ChainForm::Own; }

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

            /// The squares of what the made chain's red, green and blue decode to less the built texels they
            /// were encoded from, summed over every level: what the encoding lost.
            std::uint64_t measureSquaredError() const
            {
                assert(isMadeInBc1() && "an error measured of a chain the pass did not make in BC1");
                std::uint64_t squares = 0;
                std::vector<std::uint8_t> decoded;
                std::size_t at = 0;
                for (std::uint32_t level = 0; level < mBuilt.getLevelCount(); ++level)
                {
                    const MipLevel& laid = mBuilt.getLevel(level);
                    const std::size_t bytes = layoutOf(mFile->mFormat).levelBytes(laid.mWidth, laid.mHeight);
                    decoded.resize(std::size_t{ laid.mWidth } * laid.mHeight * 4);
                    [[maybe_unused]] const bool read = decodeBlocks(
                        mFile->mFormat, getMade().subspan(at, bytes), laid.mWidth, laid.mHeight, decoded);
                    assert(read && "a made chain that does not decode as the file's format");
                    at += bytes;

                    const std::span<const std::byte> built = mBuilt.getTexels(level);
                    for (std::size_t texel = 0; texel < decoded.size() / 4; ++texel)
                        for (std::size_t channel = 0; channel < 3; ++channel)
                        {
                            const int lost = int{ decoded[texel * 4 + channel] }
                                - std::to_integer<int>(built[texel * 4 + channel]);
                            squares += static_cast<std::uint64_t>(lost * lost);
                        }
                }
                assert(at == getMade().size() && "a made chain laid as the built levels are not");
                return squares;
            }

            /// Appends the built chain to `into` in BC1, as the pass encodes a painted colour's
            /// (`ChainBuild::encode` from its finest level).
            void encode(Misc::ByteBuffer& into) const
            {
                assert(isMadeInBc1() && "a chain encoded that the pass did not make in BC1");
                mBuilt.encode(*mFile, 0, into);
            }

        private:
            osg::ref_ptr<const osg::Image> mImage;
            std::vector<MipLevel> mLevels;
            std::vector<std::byte> mTexels;
            std::optional<TextureData> mFile;
            ChainBuild mBuilt;
            MadeChain mMade;
        };

        /// The files a run encodes, and what names them in its label.
        struct Corpus
        {
            std::vector<std::unique_ptr<PaintedChain>> mChains;

            /// The texels of every level the pass encodes, and of the finest levels alone.
            std::size_t mTexels = 0;
            std::size_t mFinestTexels = 0;

            /// What the encoding lost of them (`PaintedChain::measureSquaredError`).
            std::uint64_t mSquaredError = 0;

            /// Every byte the pass made of the files, in their order.
            Misc::HashState mMade;

            /// Adds `image` where the pass makes its whole chain in BC1, and leaves it out where not.
            void add(osg::ref_ptr<const osg::Image> image)
            {
                auto chain = std::make_unique<PaintedChain>(std::move(image));
                if (!chain->isMadeInBc1())
                    return;

                mTexels += chain->countTexels();
                mFinestTexels += chain->countTexels() - chain->countTexels(1);
                mSquaredError += chain->measureSquaredError();
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
                const double rmse
                    = std::sqrt(static_cast<double>(mSquaredError) / (3.0 * static_cast<double>(mTexels)));
                state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(mTexels));
                state.counters["rmse"] = rmse;
                state.counters["psnr"] = 20.0 * std::log10(255.0 / rmse);
                state.SetLabel(describe());
            }
        };

        /// The `.dds` files under `folder`, `sMostFiles` of them at most, spaced evenly over their sorted
        /// paths and read as the game's loader reads one, until the corpus holds `sMostTexels`. Throws
        /// `std::filesystem::filesystem_error` where the folder cannot be walked.
        Corpus readCorpus(const std::filesystem::path& folder)
        {
            std::vector<std::filesystem::path> paths;
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(
                     folder, std::filesystem::directory_options::skip_permission_denied))
                if (entry.is_regular_file()
                    && Misc::StringUtils::lowerCase(Files::pathToUnicodeString(entry.path().extension())) == ".dds")
                    paths.push_back(entry.path());
            std::ranges::sort(paths);

            osgDB::ReaderWriter* const reader = osgDB::Registry::instance()->getReaderWriterForExtension("dds");
            assert(reader != nullptr && "the build links the reader of .dds files into every program");

            // The loader's own options (`Resource::ImageManager`), so a file's format is read as the game reads it.
            const osg::ref_ptr<osgDB::Options> options = new osgDB::Options("dds_dxt1_detect_rgba");

            // **The sampled paths visited a golden step apart**, so a corpus that fills after a few files holds
            // files from across the folder and not its first names: the step is the count's golden section,
            // moved up to share no factor with it, so the walk meets every sampled path once.
            Corpus corpus;
            const std::size_t sampled = std::min(paths.size(), sMostFiles);
            auto step = std::max<std::size_t>(static_cast<std::size_t>(static_cast<double>(sampled) * 0.6180339887), 1);
            while (std::gcd(step, sampled) != 1)
                ++step;
            for (std::size_t visit = 0; visit < sampled && corpus.mFinestTexels < sMostTexels; ++visit)
            {
                std::ifstream stream(paths[visit * step % sampled * paths.size() / sampled], std::ios::binary);
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

        /// Three pictures made here: stone, wood and plaster, a square and two oblongs.
        Corpus makeCorpus()
        {
            // rgbcx's tables, for the pictures' own blocks: `ChainBuild` makes the same ones again by its own
            // flag, on this thread.
            rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);

            Corpus corpus;
            corpus.add(makePicture(512, 512, Palette{ { 0.42f, 0.38f, 0.33f }, { 0.25f, 0.27f, 0.22f } }, 1));
            corpus.add(makePicture(256, 512, Palette{ { 0.45f, 0.30f, 0.18f }, { 0.22f, 0.14f, 0.08f } }, 2));
            corpus.add(makePicture(256, 128, Palette{ { 0.75f, 0.70f, 0.60f }, { 0.55f, 0.50f, 0.45f } }, 3));
            return corpus;
        }

        /// The encoder alone: every built chain of the corpus into BC1, which is the chain the pass made of
        /// it, byte for byte.
        void encodePaintedBc1(benchmark::State& state, const Corpus& corpus)
        {
            Misc::ByteBuffer made;
            for ([[maybe_unused]] auto _ : state)
                for (const std::unique_ptr<PaintedChain>& chain : corpus.mChains)
                {
                    made.clear();
                    chain->encode(made);
                    benchmark::DoNotOptimize(made.data());
                    assert(std::ranges::equal(made, chain->getMade()) && "the encoder alone made another chain");
                }
            corpus.report(state);
        }

        /// What a preparation thread runs for a file: the pass whole, its read, its build and its encoding.
        void makePaintedChain(benchmark::State& state, const Corpus& corpus)
        {
            ChainBuild build;
            ChainPass pass(build);
            MadeChain made;
            for ([[maybe_unused]] auto _ : state)
                for (const std::unique_ptr<PaintedChain>& chain : corpus.mChains)
                {
                    pass.run(
                        ChainPass::Input{ .mImage = chain->getImage(), .mEncoding = TextureEncoding::Painted }, made);
                    benchmark::DoNotOptimize(made.mBytes.data());
                }
            corpus.report(state);
        }

        /// The corpus the run names: the folder's where `OPENMW_RTX_BENCH_TEXTURES` names one, the made
        /// pictures where it names none.
        Corpus standCorpus()
        {
            const char* const folder = std::getenv("OPENMW_RTX_BENCH_TEXTURES");
            return folder != nullptr ? readCorpus(folder) : makeCorpus();
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
        const Rtx::Corpus corpus = Rtx::standCorpus();
        if (corpus.mChains.empty())
        {
            std::fputs("no BC1 file under OPENMW_RTX_BENCH_TEXTURES makes a painted chain\n", stderr);
            return 1;
        }

        // The corpus is the run's, made once and lent to each benchmark, which the registry runs and lets go
        // of inside this block.
        benchmark::RegisterBenchmark("encodePaintedBc1", [&corpus](benchmark::State& state) {
            Rtx::encodePaintedBc1(state, corpus);
        })->Unit(benchmark::kMillisecond);
        benchmark::RegisterBenchmark("makePaintedChain", [&corpus](benchmark::State& state) {
            Rtx::makePaintedChain(state, corpus);
        })->Unit(benchmark::kMillisecond);
        benchmark::RunSpecifiedBenchmarks();
        benchmark::Shutdown();
    }
    catch (const std::filesystem::filesystem_error& error)
    {
        std::fprintf(stderr, "OPENMW_RTX_BENCH_TEXTURES: %s\n", error.what());
        return 1;
    }
    return 0;
}
