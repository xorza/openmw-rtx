#include "preparedimages.hpp"

#include <algorithm>
#include <cassert>
#include <string>

#include <components/misc/result.hpp>
#include <components/rtx/image/imagedescription.hpp>

#include "contentpreprocessor.hpp"
#include "contentstore.hpp"

namespace Rtx
{
    /// One thread that makes images: its own preprocessor, whose asks reach the store's files, the
    /// scratch a description is laid into, and the thread, last.
    struct PreparedImages::Hand
    {
        explicit Hand(ContentStore* store)
            : mContent(store, StoreReach::Disk)
        {
        }

        ContentPreprocessor mContent;
        std::vector<std::byte> mLevels;
        std::vector<MipLevel> mDescribedLevels;
        std::vector<std::byte> mDescribedTexels;
        Worker mWorker;
    };

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

    PreparedImage PreparedImages::find(const osg::Image& file, const TextureEncoding encoding)
    {
        assert(prepares(encoding) && "a colour's chain asked to be made again");

        bool queued = false;
        PreparedImage answer = mMonitor.under([&] {
            const Key key{ &file, encoding };
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

            osg::ref_ptr<const osg::Image> image;
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

    void PreparedImages::sweep()
    {
        if (mEntries.size() < mSweepAt)
            return;

        osg::ref_ptr<const osg::Image> held;
        std::erase_if(mEntries, [&](const auto& entry) {
            return entry.second.mMade && !entry.second.mFile.lock(held) && !entry.second.mWatched.lock(held)
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
                hand.mContent.chain(file, job.mKey.second, hand.mLevels);

                osg::ref_ptr<const osg::Image> made;
                if (!hand.mLevels.empty())
                {
                    hand.mDescribedLevels.clear();
                    hand.mDescribedTexels.clear();
                    const Misc::Result<TextureData, std::string> described
                        = describeImage(file, job.mKey.second, hand.mDescribedLevels, hand.mDescribedTexels);
                    if (described.isOk())
                        made = joinChain(file, described.value(), hand.mLevels);
                }

                mMonitor.hand([&] {
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
