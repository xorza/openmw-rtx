#include "preparedimages.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

#include <components/rtx/image/textureformat.hpp>

#include "contentpreprocessor.hpp"
#include "contentstore.hpp"

namespace Rtx
{
    /// One thread that makes images: its own preprocessor, whose asks reach the store's files, the
    /// chain it makes, and the thread, last.
    struct PreparedImages::Hand
    {
        explicit Hand(ContentStore* store)
            : mContent(store, StoreReach::Disk)
        {
        }

        ContentPreprocessor mContent;
        MadeChain mChain;
        Worker mWorker;
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

    PreparedImages::PreparedImages(ContentStore* const store, const std::size_t hands)
        : mStore(store)
    {
        assert(hands >= 1 && "no thread to make images on");
        for (std::size_t at = 0; at < hands; ++at)
        {
            Hand& hand = *mHands.emplace_back(std::make_unique<Hand>(mStore));
            hand.mWorker.start("chains", [this, &hand](const Platform::StopToken& stop) { serve(hand, stop); });
        }
    }

    PreparedImages::~PreparedImages()
    {
        for (const std::unique_ptr<Hand>& hand : mHands)
            hand->mWorker.stop();
    }

    PreparedImage PreparedImages::find(const osg::Image& file, const TextureEncoding encoding, const TextureWrap wrap)
    {
        assert(prepares(file, encoding) && "a colour's own chain asked to be made again");

        bool queued = false;
        PreparedImage answer = mMonitor.under([&] {
            // The wrap only where it decides what is made: a painted colour's estimate follows it.
            const Key key{ &file, encoding, encoding == TextureEncoding::Painted ? wrap : TextureWrap::Repeat };
            auto entry = mEntries.find(key);
            osg::ref_ptr<const osg::Image> standing;
            if (entry != mEntries.end() && !entry->second.mFile.lock(standing))
            {
                mEntries.erase(entry);
                entry = mEntries.end();
            }

            if (entry == mEntries.end())
            {
                sweep();
                mEntries.emplace(key, Entry{ .mFile = &file });
                mQueue.push_back(Job{ .mKey = key, .mFile = &file });
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
            mQueue.push_back(Job{ .mKey = key, .mFile = &file });
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

    void PreparedImages::sweep()
    {
        if (mEntries.size() < mSweepAt)
            return;

        osg::ref_ptr<const osg::Image> file;
        osg::ref_ptr<const MadeImage> made;
        std::erase_if(mEntries, [&](const auto& entry) {
            return entry.second.mMade && !entry.second.mFile.lock(file) && !entry.second.mWatched.lock(made)
                && entry.second.mHeld == nullptr;
        });
        mSweepAt = std::max<std::size_t>(1024, 2 * mEntries.size());
    }

    void PreparedImages::serve(Hand& hand, const Platform::StopToken& stop)
    {
        Job job;
        mMonitor.serve(
            stop, [&] { return !mQueue.empty(); },
            [&] {
                job = std::move(mQueue.front());
                mQueue.pop_front();
                ++mInFlight;
            },
            [&](const Platform::StopToken&) {
                const osg::Image& file = *job.mFile;
                const auto [image, encoding, wrap] = job.mKey;
                hand.mContent.chain(
                    ChainPass::Input{ .mImage = file, .mEncoding = encoding, .mWrap = wrap }, hand.mChain);

                // The made levels move into the image that holds them; the file's finest level stays
                // the file's (`MadeImage`).
                osg::ref_ptr<const MadeImage> made;
                const auto width = static_cast<std::uint32_t>(file.s());
                const auto height = static_cast<std::uint32_t>(file.t());
                if (hand.mChain.mForm == ChainForm::Coarser)
                    made = new MadeImage(
                        file, MadeImage::laidLayout(readFormat(file, encoding)), std::move(hand.mChain.mBytes));
                else if (hand.mChain.mForm == ChainForm::Bc7)
                    made = MadeImage::whole(bc7FormatOf(encoding), width, height, std::move(hand.mChain.mBytes));
                else if (hand.mChain.mForm == ChainForm::Own)
                    made = MadeImage::whole(readFormat(file, encoding), width, height, std::move(hand.mChain.mBytes));

                const ContentStats stats = hand.mContent.takeStats();
                mMonitor.hand([&] {
                    mStats += stats;
                    --mInFlight;
                    const auto entry = mEntries.find(job.mKey);
                    if (entry == mEntries.end())
                        return;
                    entry->second.mMade = true;
                    entry->second.mStands = made == nullptr;
                    entry->second.mHeld = made;
                    entry->second.mWatched = nullptr;
                });
                job = Job{};
            });
    }
}
