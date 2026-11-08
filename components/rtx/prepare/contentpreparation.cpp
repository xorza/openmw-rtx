#include "contentpreparation.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

#include <osg/Image>

#include <components/crashcatcher/crashnote.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/common/worker.hpp>
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
    /// One thread that walks models: the cell reader's walk, a preprocessor for the chains, the
    /// scratch a model is read into, and the thread, last.
    struct ContentPreparation::Hand
    {
        explicit Hand(ContentStore& store)
            : mWalk(nullptr, &store)
            , mChains(&store, StoreReach::Disk)
        {
        }

        TemplateWalk mWalk;
        ContentPreprocessor mChains;
        PreparedModel mModel;
        MadeChain mChain;
        std::vector<std::string> mFiles;
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
        , mSpecular(setup.mSpecular)
        , mAutoMaps(setup.mAutoMaps)
        , mSignature(signatureOf(setup))
        , mModels(std::move(models))
        , mLayers(std::move(layers))
        , mLayerNames(namesOf(mLayers))
        , mPrevious(PreparationManifest::read(setup.mManifest, mSignature))
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

    JobProgress ContentPreparation::getProgress() const
    {
        return JobProgress{ .mMade = mMade.load(), .mCount = static_cast<std::uint32_t>(mWalked.size()) };
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
        digest.add(setup.mMask);
        digest.add(setup.mSpecular);
        digest.add(setup.mAutoMaps.mNormalMaps);
        digest.add(std::span<const char>(setup.mAutoMaps.mNormalMapPattern));
        digest.add(std::span<const char>(setup.mAutoMaps.mNormalHeightMapPattern));
        digest.add(setup.mAutoMaps.mSpecularMaps);
        digest.add(std::span<const char>(setup.mAutoMaps.mSpecularMapPattern));
        return digest.getKey().mHash[0];
    }

    void ContentPreparation::serve(Hand& hand, const Platform::StopToken& stop)
    {
        while (!stop.stopRequested())
        {
            const std::size_t at = mNext.fetch_add(1);
            if (at >= mWalked.size())
                return;
            if (!mPrevious.standsAsWalked(nameOf(at), mStamps))
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
            mMade.fetch_add(1);
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

            // Each map its surface reads, as the material resolver binds it: a normal map and a
            // specular map in the encodings the frame reads them in, every other as a colour.
            const TextureEncoding specular
                = mSpecular == SpecularLayout::Classic ? TextureEncoding::Colour : TextureEncoding::Data;
            for (const PreparedPart& part : hand.mModel.mParts)
            {
                if (!part.mMaterial.mDescribed.has_value())
                    continue;
                const SurfaceDescription& described = *part.mMaterial.mDescribed;
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

                    const TextureEncoding encoding = surfaceMap == SurfaceMap::Normal ? TextureEncoding::Normal
                        : surfaceMap == SurfaceMap::Specular                          ? specular
                                                                                      : TextureEncoding::Colour;
                    chain(hand, *image, encoding);
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

        // As the cell reader reads a layer: the diffuse a colour, the normal map a normal map, and
        // where it may carry a height and does, the heights it spans.
        const Misc::Result<osg::ref_ptr<const osg::Image>, std::string> diffuse
            = mContent.getImage(VFS::Path::NormalizedView(layer.mDiffuse.c_str()));
        if (diffuse.isOk() && diffuse.value() != nullptr)
            chain(hand, *diffuse.value(), TextureEncoding::Colour);

        if (layer.mNormal.empty())
            return;
        hand.mFiles.push_back(layer.mNormal);
        const Misc::Result<osg::ref_ptr<const osg::Image>, std::string> normal
            = mContent.getImage(VFS::Path::NormalizedView(layer.mNormal.c_str()));
        if (!normal.isOk() || normal.value() == nullptr)
            return;
        chain(hand, *normal.value(), TextureEncoding::Normal);
        if (layer.mParallax && carriesHeight(*normal.value()))
            hand.mWalk.alphaRangeOf(*normal.value());
    }

    void ContentPreparation::chain(Hand& hand, const osg::Image& image, const TextureEncoding encoding)
    {
        if (!PreparedImages::prepares(image, encoding))
            return;
        std::string name(SceneUtil::EmbeddedImage::nameOf(image));
        name += static_cast<char>('0' + static_cast<int>(encoding));
        if (claim(std::move(name)))
            hand.mChains.chain(image, encoding, hand.mChain);
    }

    bool ContentPreparation::claim(std::string name)
    {
        return mMonitor.under([&] { return mClaimed.insert(std::move(name)).second; });
    }

    bool ContentPreparation::finish()
    {
        assert(getProgress().isDone() && "a preparation finished before its models were done");
        for (const std::unique_ptr<Hand>& hand : mHands)
        {
            hand->mWorker.stop();
            mStats += hand->mWalk.takeStats();
            mStats += hand->mChains.takeStats();
        }

        // Every job of this run, read or as it was: what the content list no longer names leaves the
        // manifest.
        PreparationManifest next;
        mWalkedCount = 0;
        for (std::size_t at = 0; at < mWalked.size(); ++at)
        {
            if (mWalked[at].has_value())
            {
                ++mWalkedCount;
                next.keep(nameOf(at), std::move(*mWalked[at]));
            }
            else if (const PreparationManifest::Entry* const kept = mPrevious.find(nameOf(at)))
                next.keep(nameOf(at), *kept);
        }

        // **Not written where nothing moved**: every model stood as it was, and the manifest names
        // no other, so the file on disk is the one this would write.
        if (mWalkedCount == 0 && next.size() == mPrevious.size())
            return true;
        return next.write(mManifestPath, mSignature);
    }
}
