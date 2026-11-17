#include "preparedimages.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <optional>
#include <utility>

#include <components/contentstore/store.hpp>
#include <components/resource/imageheader.hpp>
#include <components/rtx/image/textureformat.hpp>

#include "contentpreprocessor.hpp"
#include "texelreads.hpp"

namespace Rtx
{
    /// One thread that makes images: its own preprocessor, whose asks reach the store's files, the
    /// chain it makes, and the thread, last.
    struct PreparedImages::Hand
    {
        explicit Hand(ContentStore::Store* store)
            : mContent(store, ContentStore::Reach::Disk, ChainRuns::WithinMemory)
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
            || (file.getNumMipmapLevels() <= 1
                && static_cast<std::size_t>(file.s()) * static_cast<std::size_t>(file.t()) > 1)
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
        Platform::stopThenJoin(
            mHands, [](const std::unique_ptr<Hand>& hand) -> std::jthread& { return hand->mWorker; });
    }

    PreparedImage PreparedImages::find(
        const osg::Image& file, const TextureEncoding encoding, const TextureWrap wrap, const osg::Image* const partner)
    {
        const bool makes = prepares(file, encoding);
        assert((partner != nullptr) == isConverted(encoding) && "a converted slot is two files, and no other slot is");
        assert((partner == nullptr || makes) && "a converted slot's chain is made");

        // A file that holds its texels and has no chain made is what the upload reads, as it stands.
        if (!makes && !Resource::isHeader(file))
            return PreparedImage{ .mFile = &file };

        PreparedImage found;
        mMonitor.give([&] {
            const Key key = keyOf(file, encoding, wrap, partner);
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
                mQueue.push_back(Job{ .mKey = key, .mFile = &file, .mPartner = partner, .mMakes = makes });
                found = PreparedImage{ .mPending = true };
                return true;
            }

            if (!entry->second.mMade)
            {
                // Asked again before its job landed: the slot that left is not the last to want it.
                entry->second.mLeft = false;
                found = PreparedImage{ .mPending = true };
                return false;
            }

            found = answer(key, entry->second);
            return found.mPending;
        });

        return found;
    }

    PreparedImages::Key PreparedImages::keyOf(
        const osg::Image& file, const TextureEncoding encoding, const TextureWrap wrap, const osg::Image* const partner)
    {
        // A painted colour's estimate follows the wrap, and a converted slot's delit diffuse.
        const bool delit = encoding == TextureEncoding::Painted || isConverted(encoding);
        return Key{ &file, encoding, delit ? wrap : TextureWrap::Repeat, partner };
    }

    bool PreparedImages::isReady(
        const osg::Image& file, const TextureEncoding encoding, const TextureWrap wrap, const osg::Image* const partner)
    {
        return mMonitor.under([&] {
            const auto entry = mEntries.find(keyOf(file, encoding, wrap, partner));
            return entry != mEntries.end() && entry->second.mMade;
        });
    }

    PreparedImage PreparedImages::answer(const Key& key, Entry& entry)
    {
        const osg::Image& file = *std::get<0>(key);
        const auto take = [](auto& held, auto& watched, auto& into) {
            if (held != nullptr)
            {
                into = held;
                watched = held;
                held = nullptr;
                return true;
            }
            return watched.lock(into);
        };

        PreparedImage found;
        const bool chainStands = entry.mStands || take(entry.mHeld, entry.mWatched, found.mImage);
        const bool fileStands
            = !entry.mReadsFile || !Resource::isHeader(file) || take(entry.mFileHeld, entry.mFileWatched, found.mFile);
        if (entry.mReadsFile && found.mFile == nullptr && fileStands)
            found.mFile = &file;
        if (chainStands && fileStands)
            return found;

        // Read or made once and let go of by every texture since: read or made again, from the store.
        entry.mMade = false;
        mQueue.push_back(Job{
            .mKey = key, .mFile = &file, .mPartner = std::get<3>(key), .mMakes = prepares(file, std::get<1>(key)) });
        return PreparedImage{ .mPending = true };
    }

    void PreparedImages::letGo(const osg::Image& file)
    {
        mMonitor.give([&] {
            // Every slot's entry of the file that has handed out what it read and made, whatever it was read as.
            // One in flight, or holding what a slot that stood in has yet to be handed, stays.
            const auto handed = [](const Entry& entry) {
                return entry.mMade && entry.mHeld == nullptr && entry.mFileHeld == nullptr;
            };
            auto entry = mEntries.lower_bound(Key{ &file, TextureEncoding{}, TextureWrap{}, nullptr });
            while (entry != mEntries.end() && std::get<0>(entry->first) == &file)
                entry = handed(entry->second) ? mEntries.erase(entry) : std::next(entry);
            return false;
        });
        Resource::releaseTexels(file);
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

    void PreparedImages::forget(
        const osg::Image& file, const TextureEncoding encoding, const TextureWrap wrap, const osg::Image* const partner)
    {
        mMonitor.give([&] {
            const auto entry = mEntries.find(keyOf(file, encoding, wrap, partner));
            if (entry == mEntries.end())
                return false;
            if (entry->second.mMade)
                mEntries.erase(entry);
            else
            {
                entry->second.mLeft = true;
                ++mLeftPending;
            }
            return false;
        });
        Resource::releaseTexels(file);
    }

    PreparedHeld PreparedImages::getHeld()
    {
        return mMonitor.under([&] {
            PreparedHeld held{ .mLeftPending = mLeftPending };
            for (const auto& [key, entry] : mEntries)
            {
                if (entry.mHeld == nullptr && entry.mFileHeld == nullptr)
                    continue;
                ++held.mEntries;
                held.mBytes += entry.mHeld != nullptr ? entry.mHeld->getBytes().size() : 0;
                held.mBytes += entry.mFileHeld != nullptr ? entry.mFileHeld->getTotalSizeInBytesIncludingMipmaps() : 0;
            }
            return held;
        });
    }

    std::size_t PreparedImages::getScratchBytes()
    {
        assert(isIdle() && "a hand's scratch asked while it works");
        std::size_t bytes = 0;
        for (const std::unique_ptr<Hand>& hand : mHands)
            bytes += hand->mContent.chainScratch().total();
        return bytes;
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
                // **The header a model holds, keyed by its derivation**: a chain the store holds whole reads nothing of
                // the file, and a run reads its texels through the header.
                const osg::Image& file = *job.mFile;
                const auto [image, encoding, wrap, partner] = job.mKey;
                osg::ref_ptr<const osg::Image> pixels;

                std::optional<MadeLayout> layout;
                if (job.mMakes && job.mPartner != nullptr)
                {
                    const ConversionPass::Input input{ .mImage = file,
                        .mPartner = *job.mPartner,
                        .mEncoding = encoding,
                        .mWrap = wrap,
                        .mRead = &pixels };
                    if (hand.mContent.convert(input, hand.mChain))
                        layout = ConversionPass::madeLayout(input, hand.mChain.mForm);
                }
                else if (job.mMakes)
                {
                    const ChainPass::Input input{
                        .mImage = file, .mEncoding = encoding, .mWrap = wrap, .mRead = &pixels
                    };
                    if (hand.mContent.chain(input, hand.mChain))
                        layout = ChainPass::madeLayout(input, hand.mChain.mForm);
                }

                // The made levels move into the image that holds them; the file's finest level stays
                // the file's where they are coarser (`MadeImage`).
                osg::ref_ptr<const MadeImage> made;
                if (layout.has_value())
                    made = MadeImage::make(*layout, std::move(hand.mChain.mBytes));

                // The file's texels where the upload reads any of its levels: a run's read, else one now. A chain found
                // whole holds none.
                const bool readsFile = made == nullptr || !made->getWholeFormat().has_value();
                if (!readsFile)
                    pixels = nullptr;
                else if (pixels == nullptr)
                    pixels = readTexels(file, TexelRead::File, hand.mContent.texelReads());

                // **A hand lets go of its chains' scratch once the queue is drained**, as the preparation's hands
                // do: one miss on a 4096 map held some 106 MB on this thread for the session. Before the hand-over,
                // so a waiter `flush` let go finds it let go of.
                if (mMonitor.under([&] { return mQueue.empty(); }))
                    hand.mContent.letGoChainScratch();

                // **The image handed over and the job's files let go of under the lock, and no reference
                // left on this thread** once the job counts as done: a waiter that `flush` let go may drop
                // the last reference it holds and find the image or the file gone, which a reference here
                // kept alive a moment longer.
                ContentStats stats = hand.mContent.takeStats();
                // A chain the store answered whole reads nothing of its file for the upload: the figure a chain
                // found coarser, which reads the file's finest level, is told apart by.
                const std::uint32_t found
                    = stats.at(ContentPassId::Chain).mHits + stats.at(ContentPassId::Conversion).mHits;
                if (found > 0 && made != nullptr && made->getWholeFormat().has_value())
                    ++stats.mTexelReads.mChainsFoundWhole;
                mMonitor.hand([&] {
                    mStats += stats;
                    --mInFlight;
                    const auto entry = mEntries.find(job.mKey);
                    if (entry != mEntries.end() && entry->second.mLeft)
                        mEntries.erase(entry);
                    else if (entry != mEntries.end())
                    {
                        // The file's texels held where they are not the file's own image, which its holder keeps.
                        entry->second.mMade = true;
                        entry->second.mStands = made == nullptr;
                        entry->second.mReadsFile = readsFile;
                        entry->second.mHeld = made;
                        entry->second.mWatched = nullptr;
                        entry->second.mFileHeld = readsFile && pixels.get() != job.mFile.get() ? pixels : nullptr;
                        entry->second.mFileWatched = nullptr;
                    }
                    made = nullptr;
                    pixels = nullptr;
                    job = Job{};
                });
            });
    }
}
