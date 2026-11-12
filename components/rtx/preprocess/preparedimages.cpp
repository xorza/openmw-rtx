#include "preparedimages.hpp"

#include <algorithm>
#include <cassert>
#include <format>
#include <optional>
#include <utility>

#include <components/debug/debuglog.hpp>
#include <components/platform/memory.hpp>
#include <components/rtx/common/mebibytes.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/sceneutil/embeddedimage.hpp>

#include "contentpreprocessor.hpp"
#include <components/contentstore/store.hpp>

namespace Rtx
{
    /// One thread that makes images: its own preprocessor, whose asks reach the store's files, the
    /// chain it makes, and the thread, last.
    struct PreparedImages::Hand
    {
        explicit Hand(ContentStore::Store* store)
            : mContent(store, ContentStore::Reach::Disk)
        {
        }

        ContentPreprocessor mContent;
        MadeChain mChain;
        std::jthread mWorker;
    };

    bool PreparedImages::prepares(const osg::Image& file, const TextureEncoding encoding)
    {
        const TextureFormat format = readFormat(file, encoding);
        if (!isUploadable(format) && !isWidened(format))
            return false;
        return encoding != TextureEncoding::Colour
            || (file.getNumMipmapLevels() <= 1 && std::size_t(file.s()) * std::size_t(file.t()) > 1)
            || !layoutOf(format).isBlocked();
    }

    PreparedImages::PreparedImages(ContentStore::Store* const store, const std::size_t hands)
        : mStore(store)
    {
        assert(hands >= 1 && "no thread to make images on");
        for (std::size_t at = 0; at < hands; ++at)
        {
            Hand& hand = *mHands.emplace_back(std::make_unique<Hand>(mStore));
            hand.mWorker
                = Platform::startThread("chains", [this, &hand](const std::stop_token& stop) { serve(hand, stop); });
        }
    }

    PreparedImages::~PreparedImages()
    {
        // Every hand asked first and joined after, so none takes another job while the one before it
        // is joined.
        for (const std::unique_ptr<Hand>& hand : mHands)
            hand->mWorker.request_stop();
        for (const std::unique_ptr<Hand>& hand : mHands)
            hand->mWorker = std::jthread();
    }

    PreparedImage PreparedImages::find(
        const osg::Image& file, const TextureEncoding encoding, const TextureWrap wrap, const osg::Image* const partner)
    {
        assert(prepares(file, encoding) && "a colour's own chain asked to be made again");
        assert((partner != nullptr) == isConverted(encoding) && "a converted slot is two files, and no other slot is");

        bool queued = false;
        PreparedImage answer = mMonitor.under([&] {
            // The wrap only where it decides what is made: a painted colour's estimate follows it, and a
            // converted slot's delit diffuse.
            const bool delit = encoding == TextureEncoding::Painted || isConverted(encoding);
            const Key key{ &file, encoding, delit ? wrap : TextureWrap::Repeat, partner };
            auto entry = mEntries.find(key);
            if (entry != mEntries.end() && isGone(*entry))
            {
                mEntries.erase(entry);
                entry = mEntries.end();
            }

            if (entry == mEntries.end())
            {
                sweep();
                mEntries.emplace(key, Entry{ .mFile = &file, .mPartner = partner });
                mQueue.push_back(Job{ .mKey = key, .mFile = &file, .mPartner = partner });
                queued = true;
                return PreparedImage{ .mPending = true };
            }

            Entry& found = entry->second;
            if (!found.mMade)
                return PreparedImage{ .mPending = true };

            if (found.mHeld != nullptr)
            {
                PreparedImage made{ .mImage = found.mHeld };
                found.mWatched = found.mHeld;
                found.mHeld = nullptr;
                return made;
            }

            osg::ref_ptr<const MadeImage> image;
            if (found.mWatched.lock(image))
                return PreparedImage{ .mImage = image };
            if (found.mStands)
                return PreparedImage{};

            // Made once and let go of by every texture since: made again, from the store.
            found.mMade = false;
            mQueue.push_back(Job{ .mKey = key, .mFile = &file, .mPartner = partner });
            queued = true;
            return PreparedImage{ .mPending = true };
        });

        if (queued)
            mMonitor.give([] {});
        return answer;
    }

    bool PreparedImages::isIdle()
    {
        return mMonitor.under([&] { return mQueue.empty() && mInFlight == 0; });
    }

    void PreparedImages::flush()
    {
        mMonitor.await([&] { return mQueue.empty() && mInFlight == 0; });
    }

    ContentStats PreparedImages::takeStats()
    {
        return mMonitor.under([&] { return std::exchange(mStats, ContentStats{}); });
    }

    std::size_t PreparedImages::getEntryCount()
    {
        return mMonitor.under([&] { return mEntries.size(); });
    }

    bool PreparedImages::isGone(const std::pair<const Key, Entry>& entry)
    {
        // A queued job holds its files, so an entry whose file is gone is made, and no slot can ask
        // for it again: what it holds, held or watched, is no longer anyone's to collect.
        const bool paired = std::get<3>(entry.first) != nullptr;
        osg::ref_ptr<const osg::Image> standing;
        return !entry.second.mFile.lock(standing) || (paired && !entry.second.mPartner.lock(standing));
    }

    void PreparedImages::sweep()
    {
        if (mEntries.size() < mSweepAt)
            return;

        std::erase_if(mEntries, isGone);
        mSweepAt = std::max<std::size_t>(1024, 2 * mEntries.size());
    }

    bool PreparedImages::admits(const ChainScratch& scratch, const osg::Image& file)
    {
        // **A chain whose making needs more than the system has left is not made**, and its file's own
        // chain draws: allocated on this thread, it ended the game. Asked as it starts and not as it is
        // queued, since what the system has left moves with the world.
        const std::optional<std::uint64_t> available = Platform::availableMemory();
        if (!available.has_value() || scratch.total() <= *available)
            return true;

        Log(Debug::Warning) << "Ray tracing keeps the chain of \"" << SceneUtil::EmbeddedImage::nameOf(file)
                            << "\" as it stands: making it again needs "
                            << std::format("{:.1f} MiB, and the system has {:.1f} MiB left", mebibytes(scratch.total()),
                                   mebibytes(*available));
        return false;
    }

    void PreparedImages::serve(Hand& hand, const std::stop_token& stop)
    {
        Job job;
        mMonitor.serve(
            stop, [&] { return !mQueue.empty(); },
            [&] {
                job = std::move(mQueue.front());
                mQueue.pop_front();
                ++mInFlight;
            },
            [&](const std::stop_token&) {
                const osg::Image& file = *job.mFile;
                const auto [image, encoding, wrap, partner] = job.mKey;
                std::optional<MadeLayout> layout;
                if (job.mPartner != nullptr)
                {
                    const ConversionPass::Input input{
                        .mImage = file, .mPartner = *job.mPartner, .mEncoding = encoding, .mWrap = wrap
                    };
                    if (admits(ConversionPass::scratchOf(input), file))
                    {
                        hand.mContent.convert(input, hand.mChain);
                        layout = ConversionPass::madeLayout(input, hand.mChain.mForm);
                    }
                }
                else
                {
                    const ChainPass::Input input{ .mImage = file, .mEncoding = encoding, .mWrap = wrap };
                    if (admits(ChainPass::scratchOf(input), file))
                    {
                        hand.mContent.chain(input, hand.mChain);
                        layout = ChainPass::madeLayout(input, hand.mChain.mForm);
                    }
                }

                // The made levels move into the image that holds them; the file's finest level stays
                // the file's where they are coarser (`MadeImage`).
                osg::ref_ptr<const MadeImage> made;
                if (layout.has_value())
                    made = MadeImage::make(*layout, std::move(hand.mChain.mBytes));

                // **The image handed over and the job's files let go of under the lock, and no reference
                // left on this thread** once the job counts as done: a waiter that `flush` let go may drop
                // the last reference it holds and find the image or the file gone, which a reference here
                // kept alive a moment longer.
                const ContentStats stats = hand.mContent.takeStats();
                mMonitor.hand([&] {
                    mStats += stats;
                    --mInFlight;
                    const auto entry = mEntries.find(job.mKey);
                    if (entry != mEntries.end())
                    {
                        entry->second.mMade = true;
                        entry->second.mStands = made == nullptr;
                        entry->second.mHeld = made;
                        entry->second.mWatched = nullptr;
                    }
                    made = nullptr;
                    job = Job{};
                });
            });
    }
}
