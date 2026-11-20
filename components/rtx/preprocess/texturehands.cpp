#include "texturehands.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <components/contentstore/store.hpp>
#include <components/platform/thread.hpp>
#include <components/resource/imageheader.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/madeimage.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/conversionpass.hpp>
#include <components/sceneutil/embeddedimage.hpp>

#include "contentpreprocessor.hpp"
#include "texelreads.hpp"

namespace Rtx
{
    /// One thread that reads and makes texels: its own preprocessor, whose asks reach the store's files, the chain it
    /// makes, and the thread, last.
    struct TextureHands::Hand
    {
        Hand(ContentStore::Store* store, ChainAdmission& admission)
            : mContent(store, &admission)
        {
        }

        ContentPreprocessor mContent;
        PooledChain mChain;
        std::jthread mWorker;
    };

    namespace
    {
        TextureCompanion companionOf(const TextureEncoding encoding)
        {
            return encoding == TextureEncoding::Normal ? TextureCompanion::Spread : TextureCompanion::Neutral;
        }

        ImageShape shapeOfLayout(const MadeLayout& layout, const TextureEncoding encoding)
        {
            return ImageShape{ .mSource = TextureSource::File,
                .mFormat = layout.mFormat,
                .mWidth = layout.mWidth,
                .mHeight = layout.mHeight,
                .mLevels = static_cast<std::uint8_t>(layout.mCount),
                .mCompanion = companionOf(encoding) };
        }

        ImageShape shapeOfTexels(const TextureData& texels)
        {
            return ImageShape{ .mSource = TextureSource::File,
                .mFormat = texels.mFormat,
                .mWidth = texels.mWidth,
                .mHeight = texels.mHeight,
                .mLevels = static_cast<std::uint8_t>(texels.mLevels.size()),
                .mCompanion = texels.getCompanion() };
        }
    }

    bool isChainMade(const osg::Image& file, const TextureEncoding encoding)
    {
        const TextureFormat format = readFormat(file, encoding);
        if (!isUploadable(format) && !isWidened(format))
            return false;
        return encoding != TextureEncoding::Colour
            || (file.getNumMipmapLevels() <= 1
                && static_cast<std::size_t>(file.s()) * static_cast<std::size_t>(file.t()) > 1)
            || !layoutOf(format).isBlocked();
    }

    ImageShape shapeOfFile(const osg::Image& file, const osg::Image* const partner, const TextureEncoding encoding)
    {
        const TextureFormat own = readFormat(file, encoding);
        const ImageShape stands{ .mSource = TextureSource::File,
            .mFormat = own,
            .mWidth = static_cast<std::uint32_t>(std::max(file.s(), 1)),
            .mHeight = static_cast<std::uint32_t>(std::max(file.t(), 1)),
            .mLevels = static_cast<std::uint8_t>(std::max(levelCountOf(file), 1u)),
            .mCompanion = companionOf(encoding) };
        if (!isChainMade(file, encoding))
            return stands;

        std::optional<MadeLayout> made;
        if (partner != nullptr)
            made = ConversionPass::madeLayout(
                ConversionPass::Input{ .mImage = file, .mPartner = *partner, .mEncoding = encoding }, ChainForm::Bc7);
        else
        {
            // The form `ChainPass::run` takes, but for a cut, which only the texels tell.
            const ChainPass::Input input{ .mImage = file, .mEncoding = encoding };
            const bool chainless = levelCountOf(file) <= 1;
            const bool small = std::max(stands.mWidth, stands.mHeight) <= sLargestBc7ChainSide;
            const bool bc7 = encoding == TextureEncoding::Normal || !layoutOf(own).isBlocked() || (chainless && small);
            made = ChainPass::madeLayout(input, bc7 ? ChainForm::Bc7 : ChainForm::Own);
        }
        return made.has_value() ? shapeOfLayout(*made, encoding) : stands;
    }

    TextureHands::TextureHands(ContentStore::Store* const store, BufferPool& pool, const std::size_t hands)
        : mStore(store)
        , mPool(pool)
    {
        assert(hands >= 1 && "no thread to make texels on");
        for (std::size_t at = 0; at < hands; ++at)
        {
            Hand& hand = *mHands.emplace_back(std::make_unique<Hand>(mStore, mAdmission));
            hand.mWorker
                = Platform::startThread("texels", [this, &hand](const std::stop_token& stop) { serve(hand, stop); });
        }
    }

    TextureHands::~TextureHands()
    {
        Platform::stopThenJoin(
            mHands, [](const std::unique_ptr<Hand>& hand) -> std::jthread& { return hand->mWorker; });
    }

    bool TextureHands::hasCredit() const
    {
        return mMonitor.under([&] { return mCount < sInFlightMost; });
    }

    void TextureHands::ask(FeedRequest request)
    {
        assert(request.mFile != nullptr && "a request of no file");
        mMonitor.give([&] {
            assert(mCount < sInFlightMost && "a request past the feed's credit");
            Job& job = jobAt(mCount);
            job = Job{ .mRequest = std::move(request) };
            ++mCount;
            return true;
        });
    }

    std::optional<FeedArrival> TextureHands::take()
    {
        std::optional<FeedArrival> arrival;
        mMonitor.under([&] {
            // In the order asked: what a later request made waits for the first.
            while (mCount > 0 && jobAt(0).mState == JobState::Done)
            {
                Job& job = jobAt(0);
                mFront = (mFront + 1) % mJobs.size();
                --mCount;
                if (job.mForgotten)
                {
                    job = Job{};
                    continue;
                }
                arrival = FeedArrival{ .mRequest = std::move(job.mRequest), .mOutcome = std::move(*job.mOutcome) };
                job = Job{};
                return;
            }
        });
        return arrival;
    }

    void TextureHands::forget(const FeedTicket ticket)
    {
        mMonitor.under([&] {
            for (std::size_t age = 0; age < mCount; ++age)
                if (Job& job = jobAt(age); job.mRequest.mTicket == ticket)
                    job.mForgotten = true;
        });
    }

    void TextureHands::flush()
    {
        // Past the pool's most while it waits: what it waits for is held until every job is made, so a hand that
        // waited for room would wait on this waiter.
        const BufferPool::Overdraft overdraft(mPool);
        mMonitor.await([&] {
            for (std::size_t age = 0; age < mCount; ++age)
                if (jobAt(age).mState != JobState::Done)
                    return false;
            return true;
        });
    }

    bool TextureHands::isIdle()
    {
        return mMonitor.under([&] {
            for (std::size_t age = 0; age < mCount; ++age)
                if (jobAt(age).mState != JobState::Done)
                    return false;
            return true;
        });
    }

    ContentStats TextureHands::takeStats()
    {
        return mMonitor.under([&] { return std::exchange(mStats, ContentStats{}); });
    }

    std::size_t TextureHands::getScratchBytes()
    {
        assert(isIdle() && "a hand's scratch asked while it works");
        std::size_t bytes = 0;
        for (const std::unique_ptr<Hand>& hand : mHands)
            bytes += hand->mContent.chainScratch().total();
        return bytes;
    }

    std::variant<FeedMade, FeedFailed> TextureHands::make(
        Hand& hand, const FeedRequest& request, const std::stop_token& stop)
    {
        // **The header a model holds, keyed by its derivation**: a chain the store holds whole reads nothing of the
        // file, and a run reads its texels through the header.
        const osg::Image& file = *request.mFile;
        const TextureEncoding encoding = request.mEncoding;
        osg::ref_ptr<const osg::Image> pixels;
        std::optional<MadeLayout> layout;
        if (isChainMade(file, encoding))
        {
            if (request.mPartner != nullptr)
            {
                const ConversionPass::Input input{
                    .mImage = file, .mPartner = *request.mPartner, .mEncoding = encoding, .mRead = &pixels
                };
                if (hand.mContent.convert(input, request.mLevels.mFirst, mPool, stop, hand.mChain))
                    layout = ConversionPass::madeLayout(input, hand.mChain.mForm);
            }
            else
            {
                const ChainPass::Input input{ .mImage = file, .mEncoding = encoding, .mRead = &pixels };
                if (hand.mContent.chain(input, request.mLevels.mFirst, mPool, stop, hand.mChain))
                    layout = ChainPass::madeLayout(input, hand.mChain.mForm);
            }
        }

        FeedMade made;
        if (layout.has_value())
        {
            // Held from the first level the request asked, as the hand read it.
            layout->mFirst = hand.mChain.mFirst;
            const osg::ref_ptr<const MadeImage> chain = MadeImage::make(*layout, std::move(hand.mChain.mBytes));
            hand.mChain.mBytes = PooledBuffer();
            made.mLevels.assign(chain->getLevels().begin(), chain->getLevels().end());
            const MipLevel& top = made.mLevels.front();
            made.mTexels = TextureData{ .mFormat = chain->getFormat(),
                .mEncoding = encoding,
                .mWidth = top.mWidth,
                .mHeight = top.mHeight,
                .mBytes = chain->getBytes(),
                .mLevels = made.mLevels,
                .mName = SceneUtil::EmbeddedImage::nameOf(file) };
            made.mHolds = chain;
        }
        else
        {
            hand.mChain.mBytes = PooledBuffer();
            // **A slot whose texels the trace cannot read as the file holds them stands only as made**: a normal map
            // of two channels (`decodeNormal`) and a classic `_spec`, whose chain was not made.
            const TextureFormat format = readFormat(file, encoding);
            if (encoding == TextureEncoding::ConvertedSpecular
                || (encoding == TextureEncoding::Normal && format == TextureFormat::Bc5Unorm))
                return FeedFailed{ .mCause = Refused::Texture, .mWhy = "its chain could not be made" };
            if (pixels == nullptr)
                pixels = readTexels(file, TexelRead::File, hand.mContent.texelReads());
            if (pixels == nullptr)
                return FeedFailed{ .mCause = Refused::Texture, .mWhy = std::string(sNoImage) };
            Result<TextureData, std::string> described
                = describeImage(*pixels, format, encoding, made.mLevels, made.mLaid);
            if (!described.isOk())
                return FeedFailed{ .mCause = Refused::Texture, .mWhy = described.error() };
            made.mTexels = described.value();
            made.mHolds = pixels;
        }
        Resource::releaseTexels(file);
        made.mShape = shapeOfTexels(made.mTexels);
        return made;
    }

    void TextureHands::serve(Hand& hand, const std::stop_token& stop)
    {
        Job* job = nullptr;
        mMonitor.serve(
            stop,
            [&] {
                for (std::size_t age = 0; age < mCount; ++age)
                    if (jobAt(age).mState == JobState::Queued)
                        return true;
                return false;
            },
            [&] {
                for (std::size_t age = 0; age < mCount; ++age)
                    if (Job& queued = jobAt(age); queued.mState == JobState::Queued)
                    {
                        queued.mState = JobState::Making;
                        job = &queued;
                        ++mMaking;
                        return;
                    }
            },
            [&](const std::stop_token& asked) {
                // **A job forgotten before it started is cancelled**: nothing waits for what it would read.
                const bool forgotten = mMonitor.under([&] { return job->mForgotten; });
                const FeedRequest request = job->mRequest;
                std::variant<FeedMade, FeedFailed> outcome
                    = forgotten ? std::variant<FeedMade, FeedFailed>(FeedFailed{}) : make(hand, request, asked);

                // **A hand lets go of its chains' scratch once the queue is drained**: one miss on a 4096 map held
                // some 106 MB on this thread for the session.
                if (mMonitor.under([&] { return mMaking == 1; }))
                    hand.mContent.letGoChainScratch();

                ContentStats stats = hand.mContent.takeStats();
                mMonitor.hand([&] {
                    mStats += stats;
                    --mMaking;
                    job->mOutcome = std::move(outcome);
                    job->mState = JobState::Done;
                    job = nullptr;
                });
            });
    }
}
