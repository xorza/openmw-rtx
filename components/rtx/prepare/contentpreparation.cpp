#include "contentpreparation.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <utility>

#include <osg/Image>

#include <components/crashcatcher/crashnote.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/common/worker.hpp>
#include <components/rtx/image/specularlayout.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/mirror/cells/cellworld.hpp>
#include <components/rtx/mirror/cells/prepared.hpp>
#include <components/rtx/mirror/cells/templatewalk.hpp>
#include <components/rtx/mirror/surfacedescription.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/preparedimages.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/texturepass.hpp>
#include <components/sceneutil/embeddedimage.hpp>
#include <components/vfs/pathutil.hpp>

#include "filestamps.hpp"

namespace Rtx
{
    /// One thread that walks models: the cell reader's walk, whose content makes the chains as well,
    /// the scratch a model and a chain are read into, and the thread, last.
    struct ContentPreparation::Hand
    {
        explicit Hand(ContentStore& store)
            : mWalk(nullptr, &store)
        {
        }

        TemplateWalk mWalk;
        PreparedModel mModel;
        MadeChain mChain;
        std::vector<std::string> mFiles;

        /// What this thread ran on a CPU while it served, written by it alone before it ends.
        std::chrono::nanoseconds mCpu{};

        Worker mWorker;
    };

    namespace
    {
        std::vector<std::string> namesOf(const std::vector<PreparationLayer>& layers)
        {
            std::vector<std::string> names;
            names.reserve(layers.size());
            for (const PreparationLayer& layer : layers)
                names.push_back(layer.mDiffuse + '|' + layer.mNormal);
            return names;
        }
    }

    ContentPreparation::ContentPreparation(
        const PreparationSetup& setup, std::vector<std::string> models, std::vector<PreparationLayer> layers)
        : mContent(setup.mContent)
        , mStamps(setup.mStamps)
        , mStore(setup.mStore)
        , mManifestPath(setup.mManifest)
        , mMask(setup.mMask)
        , mAutoMaps(setup.mAutoMaps)
        , mSignature(signatureOf(setup))
        , mModels(std::move(models))
        , mLayers(std::move(layers))
        , mLayerNames(namesOf(mLayers))
        , mManifest(PreparationManifest::read(setup.mManifest, mSignature))
        , mDay(setup.mDay)
        , mWalked(mModels.size() + mLayers.size())
    {
        assert(setup.mHands >= 1 && "no thread to walk the models on");
        for (std::size_t at = 0; at < setup.mHands; ++at)
        {
            Hand& hand = *mHands.emplace_back(std::make_unique<Hand>(mStore));
            hand.mWorker.start("prepare", [this, &hand](const Platform::StopToken& stop) { serve(hand, stop); });
        }
    }

    ContentPreparation::~ContentPreparation()
    {
        for (const std::unique_ptr<Hand>& hand : mHands)
            hand->mWorker.stop();
    }

    JobProgress ContentPreparation::awaitProgress(const std::chrono::milliseconds patience)
    {
        const auto count = static_cast<std::uint32_t>(mWalked.size());
        std::uint32_t made = 0;
        mMonitor.awaitWithin(patience, [&] {
            made = mMade;
            return made == count;
        });
        return JobProgress{ .mMade = made, .mCount = count };
    }

    const std::string& ContentPreparation::nameOf(const std::size_t at) const
    {
        return at < mModels.size() ? mModels[at] : mLayerNames[at - mModels.size()];
    }

    std::uint64_t ContentPreparation::signatureOf(const PreparationSetup& setup)
    {
        ContentDigest digest("preparation", 1);
        digest.add(ShapePass::sVersion);
        digest.add(TexelMean::sVersion);
        digest.add(AlphaSpan::sVersion);
        digest.add(ChainPass::sVersion);
        digest.add(SpecularKind::sVersion);
        digest.add(ConversionPass::sVersion);
        digest.add(setup.mMask);
        digest.add(setup.mAutoMaps.mNormalMaps);
        digest.add(std::span<const char>(setup.mAutoMaps.mNormalMapPattern));
        digest.add(std::span<const char>(setup.mAutoMaps.mNormalHeightMapPattern));
        digest.add(setup.mAutoMaps.mSpecularMaps);
        digest.add(std::span<const char>(setup.mAutoMaps.mSpecularMapPattern));
        return digest.getKey().mHash[0];
    }

    void ContentPreparation::serve(Hand& hand, const Platform::StopToken& stop)
    {
        const std::chrono::nanoseconds started = Platform::cpuTimeOfThisThread();
        serveJobs(hand, stop);
        hand.mCpu = Platform::cpuTimeOfThisThread() - started;
    }

    void ContentPreparation::serveJobs(Hand& hand, const Platform::StopToken& stop)
    {
        while (!stop.stopRequested())
        {
            const std::size_t at = mNext.fetch_add(1);
            if (at >= mWalked.size())
                return;
            if (!mManifest.standsAsWalked(nameOf(at), mStamps))
            {
                hand.mFiles.clear();
                if (at < mModels.size())
                    walk(hand, at);
                else
                    read(hand, mLayers[at - mModels.size()]);

                std::sort(hand.mFiles.begin() + 1, hand.mFiles.end());
                hand.mFiles.erase(std::unique(hand.mFiles.begin() + 1, hand.mFiles.end()), hand.mFiles.end());
                mWalked[at] = PreparationManifest::Entry{
                    .mDigest = PreparationManifest::digestOf(hand.mFiles, mStamps),
                    .mFiles = hand.mFiles,
                };
            }
            mMonitor.hand([&] {
                if (++mMade == mWalked.size())
                    mDone = std::chrono::steady_clock::now();
            });
        }
    }

    void ContentPreparation::walk(Hand& hand, const std::size_t at)
    {
        const std::string& path = mModels[at];
        const Crash::NoteScope noted("preparing the model \"{}\"", path);
        hand.mFiles.push_back(path);

        if (const osg::ref_ptr<const osg::Node> node = mContent.getTemplate(VFS::Path::NormalizedView(path.c_str())))
        {
            hand.mModel.reuse();
            hand.mWalk.read(*node, mMask, hand.mModel);

            // Each map its surface reads, as the material resolver binds it (`SurfaceDescription::encodingOf`).
            for (const PreparedPart& part : hand.mModel.mParts)
            {
                if (!part.mMaterial.mDescribed.has_value())
                    continue;
                const SurfaceDescription& described = *part.mMaterial.mDescribed;

                // What the `_spec` map means, measured on it and kept, as the material resolver asks.
                const osg::Image* const specular = described.getTexture(SurfaceMap::Specular);
                const osg::Image* const diffuse = described.getTexture(SurfaceMap::Diffuse);
                const SpecularLayout layout
                    = specular != nullptr && !SceneUtil::EmbeddedImage::nameOf(*specular).empty()
                    ? hand.mWalk.specularLayoutOf(*specular)
                    : SpecularLayout::MetalRoughness;
                for (std::size_t map = 0; map < sSurfaceMapCount; ++map)
                {
                    const osg::Image* const image = described.getTexture(static_cast<SurfaceMap>(map));
                    if (image == nullptr)
                        continue;

                    // An image the model carries inside it is its file's, which the model's stamp
                    // stands for. And beside a diffuse, every companion the loader would look for.
                    const auto surfaceMap = static_cast<SurfaceMap>(map);
                    if (SceneUtil::EmbeddedImage::find(*image) == nullptr && !image->getFileName().empty())
                    {
                        hand.mFiles.push_back(VFS::Path::Normalized(image->getFileName()).value());
                        if (surfaceMap == SurfaceMap::Diffuse)
                            Shader::appendCompanionPaths(mAutoMaps, image->getFileName(), hand.mFiles);
                    }

                    const TextureEncoding encoding = described.encodingOf(surfaceMap, layout);
                    const TextureWrap wrap = described.getTextureUse(surfaceMap).mWrap;
                    if (isConverted(encoding))
                        convert(hand,
                            ConversionPass::Input{ .mImage = *image,
                                .mPartner = encoding == TextureEncoding::ConvertedColour ? *specular : *diffuse,
                                .mEncoding = encoding,
                                .mWrap = wrap });
                    else
                        chain(hand, ChainPass::Input{ .mImage = *image, .mEncoding = encoding, .mWrap = wrap });
                }
            }

            // The template and its images let go of, so the loader's cache may drop them.
            hand.mModel.reuse();
        }
    }

    void ContentPreparation::read(Hand& hand, const PreparationLayer& layer)
    {
        const Crash::NoteScope noted("preparing the ground layer \"{}\"", layer.mDiffuse);
        hand.mFiles.push_back(layer.mDiffuse);

        // As the cell reader reads a layer: the diffuse as `layerEncodingOf` binds it, the normal map a
        // normal map, and where it may carry a height and does, the heights it spans.
        const Result<osg::ref_ptr<const osg::Image>, std::string> diffuse
            = mContent.getImage(VFS::Path::NormalizedView(layer.mDiffuse.c_str()));
        if (diffuse.isOk() && diffuse.value() != nullptr)
            chain(hand,
                ChainPass::Input{ .mImage = *diffuse.value(),
                    .mEncoding = layerEncodingOf(layer.mDiffuseSpec),
                    .mWrap = TextureWrap::Repeat });

        if (layer.mNormal.empty())
            return;
        hand.mFiles.push_back(layer.mNormal);
        const Result<osg::ref_ptr<const osg::Image>, std::string> normal
            = mContent.getImage(VFS::Path::NormalizedView(layer.mNormal.c_str()));
        if (!normal.isOk() || normal.value() == nullptr)
            return;
        chain(hand, ChainPass::Input{ .mImage = *normal.value(), .mEncoding = TextureEncoding::Normal });
        if (layer.mParallax && carriesHeight(*normal.value()))
            hand.mWalk.alphaRangeOf(*normal.value());
    }

    void ContentPreparation::chain(Hand& hand, const ChainPass::Input& input)
    {
        if (!PreparedImages::prepares(input.mImage, input.mEncoding))
            return;

        // Claimed by its name, its encoding and, for a painted colour, its wrap: what the store keys it by.
        std::string name(SceneUtil::EmbeddedImage::nameOf(input.mImage));
        name += static_cast<char>('0' + static_cast<int>(input.mEncoding));
        if (input.mEncoding == TextureEncoding::Painted)
            name += static_cast<char>('0' + static_cast<int>(input.mWrap));
        if (claim(std::move(name)))
            hand.mWalk.storeChain(input, hand.mChain);
    }

    void ContentPreparation::convert(Hand& hand, const ConversionPass::Input& input)
    {
        // Claimed by both names, the encoding and the wrap: what the store keys it by.
        std::string name(SceneUtil::EmbeddedImage::nameOf(input.mImage));
        name += '|';
        name += SceneUtil::EmbeddedImage::nameOf(input.mPartner);
        name += static_cast<char>('0' + static_cast<int>(input.mEncoding));
        name += static_cast<char>('0' + static_cast<int>(input.mWrap));
        if (claim(std::move(name)))
            hand.mWalk.storeConversion(input, hand.mChain);
    }

    bool ContentPreparation::claim(std::string name)
    {
        return mMonitor.under([&] { return mClaimed.insert(std::move(name)).second; });
    }

    bool ContentPreparation::finish()
    {
        assert(awaitProgress(std::chrono::milliseconds::zero()).isDone()
            && "a preparation finished before its models were done");
        mTimes = PreparationTimes{
            .mWall = mMonitor.under([&] { return mDone; }) - mStarted, .mThreadsCpu = {}, .mThreads = mHands.size()
        };
        for (const std::unique_ptr<Hand>& hand : mHands)
        {
            hand->mWorker.stop();
            mStats += hand->mWalk.takeStats();
            mTimes.mThreadsCpu += hand->mCpu;
        }

        // Every job of this run filed or named, and what no run named for a month dropped: another
        // content list's models stand through a switch to this one and back.
        mWalkedCount = 0;
        for (std::size_t at = 0; at < mWalked.size(); ++at)
        {
            if (mWalked[at].has_value())
            {
                ++mWalkedCount;
                mManifest.keep(nameOf(at), *mWalked[at], mDay);
            }
            else
                mManifest.name(nameOf(at), mDay);
        }
        mManifest.dropUnnamed(mDay);

        // **Not written where nothing moved**: every model stood as it was, named today already, and
        // none was dropped, so the file on disk is the one this would write.
        if (!mManifest.isChanged())
            return true;
        return mManifest.write(mManifestPath, mSignature);
    }
}
