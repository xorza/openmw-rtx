#include "contentpreparation.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <osg/Image>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/hash.hpp>
#include <components/misc/result.hpp>
#include <components/platform/heap.hpp>
#include <components/platform/thread.hpp>
#include <components/rtx/image/specularlayout.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/mirror/cells/cellworld.hpp>
#include <components/rtx/mirror/cells/prepared.hpp>
#include <components/rtx/mirror/cells/templatewalk.hpp>
#include <components/rtx/mirror/surfacedescription.hpp>
#include <components/rtx/preprocess/contentpreprocessor.hpp>
#include <components/rtx/preprocess/texelreads.hpp>
#include <components/rtx/preprocess/texture/chainadmission.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/filefactspass.hpp>
#include <components/rtx/preprocess/texturehands.hpp>
#include <components/sceneutil/embeddedimage.hpp>
#include <components/vfs/pathutil.hpp>

#include "filestamps.hpp"

namespace Rtx
{
    /// One thread that walks models and makes chains: the cell reader's walk, whose content makes the chains
    /// as well, the scratch a model and a chain are read into, what its walk claimed and met, and the
    /// thread, last.
    struct ContentPreparation::Hand
    {
        explicit Hand(ContentStore::Store& store)
            : mContent(&store)
            , mWalk(nullptr)
        {
        }

        /// What this thread computes from the content, its every read reaching the store's files.
        ThreadContent mContent;
        TemplateWalk mWalk;
        PreparedModel mModel;
        std::vector<std::string> mFiles;
        std::vector<StoredKey> mKeys;

        /// Its number among the threads, which the dispatcher knows it by.
        std::size_t mIndex = 0;

        /// The chains the walk claimed, with what each needs, queued once it ends; and the images it met,
        /// each once, and their bytes.
        std::vector<QueuedChain> mQueued;
        std::vector<const osg::Image*> mMet;
        std::uint64_t mMetBytes = 0;

        /// The paths the loader may let go of once this thread's job ends.
        std::vector<std::string> mLetGo;

        /// What this thread ran on a CPU while it served, written by it alone before it ends.
        std::chrono::nanoseconds mCpu{};

        std::jthread mWorker;
    };

    namespace
    {
        /// How long a chain of `image` read as `encoding` is estimated to take on one thread: its texels,
        /// the chain's a third past the finest's, at what one thread builds and encodes a second, from the
        /// benches (`docs/research/2026-11-09-content-preparation-cost.md`): BC1 at rgbcx's level 10
        /// 7.4 million texels a second, BC7 in one mode 20.6 million and a normal map's build past it. An
        /// estimate orders a backfill and admits nothing.
        double estimatedSeconds(const osg::Image& image, const TextureEncoding encoding)
        {
            constexpr double normalRate = 13.0e6;
            constexpr double otherRate = 10.0e6;
            const double rate = encoding == TextureEncoding::Normal ? normalRate : otherRate;
            const double texels = static_cast<double>(std::max(image.s(), 0)) * std::max(image.t(), 0);
            return texels * 4.0 / 3.0 / rate;
        }

        /// What a queued chain needs, read off its images' headers: its make's scratch, its images' bytes and
        /// its time.
        ChainNeed needOf(const osg::Image& image, const osg::Image* partner, const TextureEncoding encoding)
        {
            const ChainScratch scratch = partner != nullptr
                ? ConversionPass::scratchOf(
                      ConversionPass::Input{ .mImage = image, .mPartner = *partner, .mEncoding = encoding })
                : ChainPass::scratchOf(ChainPass::Input{ .mImage = image, .mEncoding = encoding });
            std::uint64_t images = image.getTotalSizeInBytesIncludingMipmaps();
            if (partner != nullptr)
                images += partner->getTotalSizeInBytesIncludingMipmaps();
            return ChainNeed{
                .mScratch = scratch, .mImageBytes = images, .mSeconds = estimatedSeconds(image, encoding)
            };
        }

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
        , mBudget(setup.mBudget)
        , mAdmission(setup.mMemory)
        , mWalked(mModels.size() + mLayers.size())
        , mStood(mWalked.size())
        , mDispatcher(setup.mHands, mWalked.size(), setup.mBudget)
    {
        assert(setup.mHands >= 1 && "no thread to walk the models on");
        for (std::size_t at = 0; at < setup.mHands; ++at)
        {
            Hand& hand = *mHands.emplace_back(std::make_unique<Hand>(mStore));
            hand.mIndex = at;
            hand.mWorker
                = Platform::startThread("prepare", [this, &hand](const std::stop_token& stop) { serve(hand, stop); });
        }
    }

    ContentPreparation::~ContentPreparation()
    {
        Platform::stopThenJoin(
            mHands, [](const std::unique_ptr<Hand>& hand) -> std::jthread& { return hand->mWorker; });
    }

    JobProgress ContentPreparation::awaitProgress(const std::chrono::milliseconds patience)
    {
        // **The walks counted, the last held back until every chain is made too**, so a waiter does not
        // take the queue's tail for the end.
        const auto count = static_cast<std::uint32_t>(mWalked.size());
        std::unique_lock lock(mMutex);
        mToWaiter.wait_for(lock, patience, [&] { return mFinished; });
        return JobProgress::of(mMade, count, mFinished);
    }

    double ContentPreparation::secondsNow() const
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - mStarted).count();
    }

    void ContentPreparation::use(const std::string& path)
    {
        ++mUses[path];
    }

    void ContentPreparation::endUse(const std::string& path, std::vector<std::string>& letGo)
    {
        const auto found = mUses.find(path);
        if (found == mUses.end() || --found->second == 0)
        {
            if (found != mUses.end())
                mUses.erase(found);
            letGo.push_back(path);
        }
    }

    void ContentPreparation::letGoOf(std::vector<std::string>& paths)
    {
        if (paths.empty())
            return;
        mContent.letGo(paths);
        paths.clear();
    }

    const std::string& ContentPreparation::nameOf(const std::size_t at) const
    {
        return at < mModels.size() ? mModels[at] : mLayerNames[at - mModels.size()];
    }

    std::uint64_t ContentPreparation::signatureOf(const PreparationSetup& setup)
    {
        // Every pass's version and the file key's, so whichever of them moves leaves no model standing: written out
        // pass by pass, the list left a fact pass and the file key out.
        ContentStore::Digest digest("preparation", TemplateWalk::sVersion);
        digest.add(std::span<const std::uint32_t>(sContentPassVersions));
        digest.add(sFileKeyVersion);
        digest.add(setup.mMask);
        digest.add(setup.mAutoMaps.mNormalMaps);
        digest.add(std::span<const char>(setup.mAutoMaps.mNormalMapPattern));
        digest.add(std::span<const char>(setup.mAutoMaps.mNormalHeightMapPattern));
        digest.add(setup.mAutoMaps.mSpecularMaps);
        digest.add(std::span<const char>(setup.mAutoMaps.mSpecularMapPattern));
        return digest.getKey().mHash[0];
    }

    void ContentPreparation::serve(Hand& hand, const std::stop_token& stop)
    {
        const std::chrono::nanoseconds started = Platform::cpuTimeOfThisThread();
        serveJobs(hand, stop);
        hand.mCpu = Platform::cpuTimeOfThisThread() - started;
    }

    void ContentPreparation::serveJobs(Hand& hand, const std::stop_token& stop)
    {
        // Under the lock, or the wake could land between this thread's ask and its wait.
        const std::stop_callback woken(stop, [this] {
            const std::scoped_lock lock(mMutex);
            mToHands.notify_all();
        });

        std::unique_lock lock(mMutex);
        while (!stop.stop_requested())
        {
            const Dispatch dispatch = mDispatcher.next(hand.mIndex, secondsNow());
            if (dispatch.mWakes)
                mToHands.notify_all();

            switch (dispatch.mKind)
            {
                case Dispatch::Kind::Wait:
                    mToHands.wait(lock);
                    continue;
                case Dispatch::Kind::Finished:
                    mFinished = true;
                    mToHands.notify_all();
                    mToWaiter.notify_all();
                    return;
                case Dispatch::Kind::LetGo:
                {
                    lock.unlock();
                    hand.mContent.mPreprocessor.letGoChainScratch();
                    hand.mModel.letGoBuffers();
                    // glibc keeps what a thread's arena freed below its mapping threshold until a trim, so
                    // the room reaches the system only through it.
                    Platform::trimHeap();
                    lock.lock();
                    mDispatcher.letGo(hand.mIndex);
                    mToHands.notify_all();
                    continue;
                }
                case Dispatch::Kind::Walk:
                {
                    lock.unlock();
                    walkJob(hand, dispatch.mJob);
                    lock.lock();
                    for (auto& at : hand.mQueued)
                    {
                        [[maybe_unused]] const std::size_t chain
                            = mDispatcher.queue(needOf(*at.mImage, at.mPartner.get(), at.mEncoding));
                        assert(chain == mChains.size() && "a chain numbered apart from its queue");
                        use(std::string(SceneUtil::EmbeddedImage::nameOf(*at.mImage)));
                        if (at.mPartner != nullptr)
                            use(std::string(SceneUtil::EmbeddedImage::nameOf(*at.mPartner)));
                        mChains.push_back(std::move(at));
                    }
                    hand.mQueued.clear();
                    for (const std::string& file : hand.mFiles)
                        if (!mUses.contains(file))
                            hand.mLetGo.push_back(file);
                    ++mMade;
                    break;
                }
                case Dispatch::Kind::Chain:
                {
                    // **Alone past the budget, and not at all past what the system has left**: the chain
                    // is the file's own, as play keeps it (`TextureHands`).
                    // Outside the lock, the log and the ask of the system's memory both: the chain is this
                    // hand's alone, and the other hands queue and take beside it.
                    const QueuedChain& queuedChain = mChains[dispatch.mJob];
                    lock.unlock();
                    std::optional<ChainAdmission::Ticket> admitted = ChainAdmission::Ticket{};
                    if (dispatch.mAlone)
                    {
                        Log(Debug::Info) << "The content's preparation makes the chain of \""
                                         << SceneUtil::EmbeddedImage::nameOf(*queuedChain.mImage)
                                         << "\" alone: it needs more than the whole budget of memory";
                        admitted = mAdmission.admit(
                            needOf(*queuedChain.mImage, queuedChain.mPartner.get(), queuedChain.mEncoding).mScratch,
                            *queuedChain.mImage);
                    }
                    if (admitted.has_value())
                        makeChain(hand, queuedChain);
                    else
                        hand.mKeys.clear();
                    lock.lock();
                    QueuedChain& made = mChains[dispatch.mJob];
                    for (const StoredKey& key : hand.mKeys)
                        mChainKeys.emplace_back(made.mWalk, key);
                    endUse(std::string(SceneUtil::EmbeddedImage::nameOf(*made.mImage)), hand.mLetGo);
                    if (made.mPartner != nullptr)
                        endUse(std::string(SceneUtil::EmbeddedImage::nameOf(*made.mPartner)), hand.mLetGo);
                    made.mImage = nullptr;
                    made.mPartner = nullptr;
                    break;
                }
            }

            const ChainScratch kept = hand.mContent.mPreprocessor.chainScratch();
            mDispatcher.ended(hand.mIndex, kept, hand.mModel.getRoomBytes(), hand.mMetBytes);
            mDone = std::chrono::steady_clock::now();
            mToHands.notify_all();
            mToWaiter.notify_all();

            lock.unlock();
            letGoOf(hand.mLetGo);
            lock.lock();
        }
    }

    void ContentPreparation::meet(Hand& hand, const osg::Image& image)
    {
        if (std::find(hand.mMet.begin(), hand.mMet.end(), &image) != hand.mMet.end())
            return;
        hand.mMet.push_back(&image);

        // An image with no pixels weighs nothing, and is not asked: OSG answers nought for its type
        // of nought through `osg::notify`, whose one stream two preparation threads would write at once.
        if (image.data() != nullptr)
            hand.mMetBytes += image.getTotalSizeInBytesIncludingMipmaps();
    }

    void ContentPreparation::walkJob(Hand& hand, const std::size_t next)
    {
        // **The ground layers first**, as a scheduler hands out its longest jobs first: they hold the
        // largest maps, and served last they left the PBR replacers' last seconds to a few threads.
        const std::size_t at = next < mLayers.size() ? mModels.size() + next : next - mLayers.size();
        // Before the manifest's question, which reads every file the model named for its stamps.
        const Crash::NoteScope noted("preparing \"{}\"", nameOf(at));
        hand.mFiles.clear();
        hand.mMet.clear();
        hand.mMetBytes = 0;
        if (const std::optional<std::uint64_t> stood = mManifest.standingDigest(nameOf(at), mStamps, mStore))
        {
            mStood[at] = stood;
            return;
        }

        hand.mKeys.clear();
        hand.mContent.mPreprocessor.collectKeys(&hand.mKeys);
        if (at < mModels.size())
            walk(hand, at);
        else
            read(hand, mLayers[at - mModels.size()]);
        hand.mContent.mPreprocessor.collectKeys(nullptr);

        for (QueuedChain& queued : hand.mQueued)
            queued.mWalk = at;

        std::sort(hand.mFiles.begin() + 1, hand.mFiles.end());
        hand.mFiles.erase(std::unique(hand.mFiles.begin() + 1, hand.mFiles.end()), hand.mFiles.end());
        std::sort(hand.mKeys.begin(), hand.mKeys.end(),
            [](const StoredKey& a, const StoredKey& b) { return a.mKey.mHash < b.mKey.mHash; });
        hand.mKeys.erase(std::unique(hand.mKeys.begin(), hand.mKeys.end()), hand.mKeys.end());
        mWalked[at] = PreparationManifest::Entry{
            .mDigest = PreparationManifest::digestOf(hand.mFiles, mStamps),
            .mFiles = hand.mFiles,
            .mKeys = hand.mKeys,
        };
    }

    void ContentPreparation::makeChain(Hand& hand, const QueuedChain& queued)
    {
        hand.mKeys.clear();
        hand.mContent.mPreprocessor.collectKeys(&hand.mKeys);
        // The header a model holds, whose texels a run reads for the make alone: a chain the store holds reads none.
        if (queued.mPartner != nullptr)
            std::ignore = hand.mContent.mPreprocessor.prepareConversion(ConversionPass::Input{ .mImage = *queued.mImage,
                .mPartner = *queued.mPartner,
                .mEncoding = queued.mEncoding,
                .mWhy = TexelRead::Preparation,
                .mPartnerWhy = TexelRead::Preparation });
        else
            std::ignore = hand.mContent.mPreprocessor.prepareChain(ChainPass::Input{
                .mImage = *queued.mImage, .mEncoding = queued.mEncoding, .mWhy = TexelRead::Preparation });
        hand.mContent.mPreprocessor.collectKeys(nullptr);
    }

    void ContentPreparation::walk(Hand& hand, const std::size_t at)
    {
        const std::string& path = mModels[at];
        hand.mFiles.push_back(path);

        if (const osg::ref_ptr<const osg::Node> node = mContent.getTemplate(VFS::Path::NormalizedView(path.c_str())))
        {
            hand.mModel.reuse();
            hand.mWalk.read(*node, mMask, hand.mModel, hand.mContent);

            // Each map its surface reads, as the material resolver binds it (`SurfaceDescription::encodingOf`).
            for (const PreparedPart& part : hand.mModel.mParts)
            {
                if (!part.mMaterial.mDescribed.has_value())
                    continue;
                const SurfaceDescription& described = *part.mMaterial.mDescribed;

                // What the `_spec` map means, measured on it and kept, as the material resolver asks.
                const osg::Image* const specular = described.getTexture(SurfaceMap::Specular);
                const SpecularLayout layout
                    = specular != nullptr && !SceneUtil::EmbeddedImage::nameOf(*specular).empty()
                    ? hand.mContent.factsOf(*specular).mSpecularLayout
                    : SpecularLayout::MetalRoughness;
                for (std::size_t map = 0; map < sSurfaceMapCount; ++map)
                {
                    const osg::Image* const image = described.getTexture(static_cast<SurfaceMap>(map));
                    if (image == nullptr)
                        continue;
                    meet(hand, *image);

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
                    if (const osg::Image* const partner = described.partnerOf(surfaceMap, layout))
                        convert(hand,
                            ConversionPass::Input{ .mImage = *image, .mPartner = *partner, .mEncoding = encoding });
                    else
                        chain(hand, ChainPass::Input{ .mImage = *image, .mEncoding = encoding });
                }
            }

            // The template and its images let go of, so the loader's cache may drop them: a queued chain
            // holds its own.
            hand.mModel.reuse();
        }
    }

    void ContentPreparation::read(Hand& hand, const PreparationLayer& layer)
    {
        hand.mFiles.push_back(layer.mDiffuse);

        // As the cell reader reads a layer: the diffuse a colour, the normal map a normal map, and where it may carry a
        // height and does, the heights it spans.
        const Result<osg::ref_ptr<const osg::Image>, std::string> diffuse
            = mContent.getImage(VFS::Path::NormalizedView(layer.mDiffuse.c_str()));
        if (diffuse.isOk() && diffuse.value() != nullptr)
            meet(hand, *diffuse.value());
        if (diffuse.isOk() && diffuse.value() != nullptr)
            chain(hand, ChainPass::Input{ .mImage = *diffuse.value(), .mEncoding = TextureEncoding::Colour });

        if (layer.mNormal.empty())
            return;
        hand.mFiles.push_back(layer.mNormal);
        const Result<osg::ref_ptr<const osg::Image>, std::string> normal
            = mContent.getImage(VFS::Path::NormalizedView(layer.mNormal.c_str()));
        if (!normal.isOk() || normal.value() == nullptr)
            return;
        meet(hand, *normal.value());
        chain(hand, ChainPass::Input{ .mImage = *normal.value(), .mEncoding = TextureEncoding::Normal });
    }

    void ContentPreparation::chain(Hand& hand, const ChainPass::Input& input)
    {
        if (!isChainMade(input.mImage, input.mEncoding))
            return;

        // Claimed by its name and its encoding: what the store keys it by.
        if (claim(ChainIdentity{ .mImage = std::string(SceneUtil::EmbeddedImage::nameOf(input.mImage)),
                .mPartner = {},
                .mEncoding = input.mEncoding }))
            hand.mQueued.push_back(
                QueuedChain{ .mImage = &input.mImage, .mPartner = nullptr, .mEncoding = input.mEncoding, .mWalk = 0 });
    }

    void ContentPreparation::convert(Hand& hand, const ConversionPass::Input& input)
    {
        // Claimed by both names and the encoding: what the store keys it by.
        if (claim(ChainIdentity{ .mImage = std::string(SceneUtil::EmbeddedImage::nameOf(input.mImage)),
                .mPartner = std::string(SceneUtil::EmbeddedImage::nameOf(input.mPartner)),
                .mEncoding = input.mEncoding }))
            hand.mQueued.push_back(QueuedChain{
                .mImage = &input.mImage, .mPartner = &input.mPartner, .mEncoding = input.mEncoding, .mWalk = 0 });
    }

    std::size_t ContentPreparation::ChainIdentityHash::operator()(const ChainIdentity& identity) const
    {
        std::size_t seed = 0;
        Misc::hashCombine(seed, identity.mImage);
        Misc::hashCombine(seed, identity.mPartner);
        Misc::hashCombine(seed, static_cast<std::size_t>(identity.mEncoding));
        return seed;
    }

    bool ContentPreparation::claim(ChainIdentity identity)
    {
        const std::scoped_lock lock(mMutex);
        return mClaimed.insert(std::move(identity)).second;
    }

    bool ContentPreparation::finish()
    {
        assert(awaitProgress(std::chrono::milliseconds::zero()).isDone()
            && "a preparation finished before its models were done");
        mTimes = PreparationTimes{ .mWall = mDone - mStarted,
            .mThreadsCpu = {},
            .mThreads = mHands.size(),
            .mBudget = mBudget,
            .mHeldMost = mDispatcher.getHeldMost() };
        Platform::stopThenJoin(
            mHands, [](const std::unique_ptr<Hand>& hand) -> std::jthread& { return hand->mWorker; });
        for (const std::unique_ptr<Hand>& hand : mHands)
        {
            mStats += hand->mContent.mPreprocessor.takeStats();
            mTimes.mThreadsCpu += hand->mCpu;
        }

        // Each chain's keys filed under the walk that claimed it, each once.
        for (const auto& [walk, key] : mChainKeys)
            if (mWalked[walk].has_value())
                mWalked[walk]->mKeys.push_back(key);
        for (std::optional<PreparationManifest::Entry>& walked : mWalked)
            if (walked.has_value())
            {
                std::vector<StoredKey>& keys = walked->mKeys;
                std::sort(keys.begin(), keys.end(),
                    [](const StoredKey& a, const StoredKey& b) { return a.mKey.mHash < b.mKey.mHash; });
                keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
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
            else if (mStood[at].has_value())
                mManifest.name(nameOf(at), *mStood[at], mDay);
        }
        mManifest.dropUnnamed(mDay);

        // **Not written where nothing moved**: every model stood as it was, named today already, and
        // none was dropped, so the file on disk is the one this would write.
        if (!mManifest.isChanged())
            return true;
        return mManifest.write(mManifestPath, mSignature);
    }
}
