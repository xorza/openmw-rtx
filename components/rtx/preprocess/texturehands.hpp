#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <osg/Image>

#include <components/misc/monitor.hpp>
#include <components/rtx/common/bufferpool.hpp>
#include <components/rtx/image/imageshape.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>

#include "contentstats.hpp"
#include "texturefeed.hpp"

namespace ContentStore
{
    class Store;
}

namespace Rtx
{
    /// Whether `file` read as `encoding` has its chain made: none in a format the scene refuses (`describeImage`); a
    /// normal or data map's, whose replacers' chains use wrong filters; any file's that carries none and is more than a
    /// texel; and a loose file's, encoded to BC7 (`ChainPass`). A block colour's own chain stands as the file holds it.
    bool isChainMade(const osg::Image& file, TextureEncoding encoding);

    /// **The shape a file's texels stand as**, from its header alone: its own chain, or the chain made of it in the
    /// form `ChainPass` makes for the file's kind. A BC1 file of one level with a cut is made in BC7 where this says
    /// its own format, which only its texels tell: the completion answers the shape it was made in.
    ImageShape shapeOfFile(const osg::Image& file, const osg::Image* partner, TextureEncoding encoding);

    /// **The feed over threads of their own** (`TextureFeed`), in place of a table of what was read: each request a job
    /// in a ring of `sInFlightMost`, a hand reading the file's texels through its header or making its chain again
    /// from its finest level (`ChainPass`, `ConversionPass`) through the store, and describing what the device uploads.
    /// Completions are taken in the order asked. Pinned: the threads reach every member.
    class TextureHands final : public TextureFeed
    {
    public:
        /// The most requests in flight: a hand-over's arrivals at the busiest cell.
        static constexpr std::size_t sInFlightMost = 64;

        /// **The buffers the made chains are read into, 256 MiB in use and free**: a 4096-square map's whole chain in
        /// BC7 is 21 MiB, so a dozen stand between the hands and the uploads before a hand waits for one given back.
        static constexpr std::size_t sPoolBytes = std::size_t{ 256 } << 20;

        /// @param store where the chains are found and kept, or null for none.
        /// @param pool what the made chains are read into, which outlives this and every completion it hands out.
        /// @param hands how many threads make them.
        TextureHands(ContentStore::Store* store, BufferPool& pool, std::size_t hands);

        /// Stops every thread, dropping what is queued.
        ~TextureHands();

        TextureHands(const TextureHands&) = delete;
        TextureHands& operator=(const TextureHands&) = delete;

        bool hasCredit() const override;
        void ask(FeedRequest request) override;
        std::optional<FeedArrival> take() override;
        void forget(FeedTicket ticket) override;

        /// Waits until every request asked is made, its hands taking past the pool's most meanwhile
        /// (`BufferPool::Overdraft`): what a settled run waits on, so two processes stand one image on one frame.
        void flush();

        /// Whether nothing is queued or being made.
        bool isIdle();

        /// What every thread's preprocessor cost since the last take, its jobs' whole.
        ContentStats takeStats();

        /// What every hand keeps of its chains' scratch, asked while `isIdle`: nought, as a hand lets go of it once
        /// the queue is drained.
        std::size_t getScratchBytes();

    private:
        enum class JobState : std::uint8_t
        {
            Queued,
            Making,
            Done,
        };

        struct Job
        {
            FeedRequest mRequest;
            JobState mState = JobState::Queued;
            bool mForgotten = false;
            std::optional<std::variant<FeedMade, FeedFailed>> mOutcome{};
        };

        struct Hand;

        void serve(Hand& hand, const std::stop_token& stop);

        /// What a hand makes of `request`.
        std::variant<FeedMade, FeedFailed> make(Hand& hand, const FeedRequest& request, const std::stop_token& stop);

        Job& jobAt(std::size_t age) { return mJobs[(mFront + age) % mJobs.size()]; }

        ContentStore::Store* const mStore;

        BufferPool& mPool;

        mutable Misc::Monitor mMonitor;

        /// The jobs in the order asked, `mCount` of them from `mFront` on: a ring, so a request allocates nothing.
        std::array<Job, sInFlightMost> mJobs;
        std::size_t mFront = 0;
        std::size_t mCount = 0;
        std::size_t mMaking = 0;
        ContentStats mStats;

        /// Each thread's own preprocessor and its thread, last, so the threads are joined before the members they read
        /// go.
        std::vector<std::unique_ptr<Hand>> mHands;
    };
}
