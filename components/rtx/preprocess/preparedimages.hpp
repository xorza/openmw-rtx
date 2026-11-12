#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include <osg/Image>
#include <osg/observer_ptr>
#include <osg/ref_ptr>

#include <components/misc/monitor.hpp>
#include <components/platform/thread.hpp>
#include <components/rtx/image/madeimage.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/preprocess/texture/chainscratch.hpp>

#include "contentstats.hpp"

namespace ContentStore
{
    class Store;
}

namespace Rtx
{
    /// What `PreparedImages::find` answers for a file.
    struct PreparedImage
    {
        /// The file's chain made again, which the upload reads in the file's place; null where
        /// nothing is made of it.
        osg::ref_ptr<const MadeImage> mImage{};

        /// Whether it is being made, so the caller describes the file as it stands for now and asks
        /// again: false where it is made, or where its own chain stands.
        bool mPending = false;
    };

    /// Files with their chains made again from their finest levels (`ChainPass`), on threads of their own
    /// (`docs/architecture.md`, "A chain is built on a worker"): a slot draws its file as it stands
    /// until its image is made, and is then made again. A made image is held until it is first handed
    /// out, and watched after it, so the texture that holds it decides how long it stays. One for a
    /// process, shared by every thread that asks. Pinned: the threads reach every member.
    class PreparedImages
    {
    public:
        /// Whether `file` read as `encoding` has its chain made: none in a format the scene refuses
        /// (`describeImage`); a normal or data map's, whose replacers' chains use wrong filters; a painted
        /// colour's, delit; any file's that carries none and is more than a texel; and a loose file's,
        /// encoded to BC7 (`ChainPass`). A block colour's own chain stands as the file holds it.
        static bool prepares(const osg::Image& file, TextureEncoding encoding);

        /// Whether a chain whose making holds `scratch` at most is made of `file`: not where it needs more
        /// than the system has left (`Platform::availableMemory`), which is logged. The one rule the
        /// loading screen's preparation and play's chains are both held to.
        static bool admits(const ChainScratch& scratch, const osg::Image& file);

        /// @param store where the chains are found and kept, or null for none.
        /// @param hands how many threads make them.
        PreparedImages(ContentStore::Store* store, std::size_t hands);

        /// Stops every thread, dropping what is queued.
        ~PreparedImages();

        PreparedImages(const PreparedImages&) = delete;
        PreparedImages& operator=(const PreparedImages&) = delete;
        PreparedImages(PreparedImages&&) = delete;
        PreparedImages& operator=(PreparedImages&&) = delete;

        /// `file`, read as `encoding` by a slot that addresses it as `wrap`, with its chain made again:
        /// made, or queued and pending, or nothing made of it. `prepares(encoding)` holds. Never waits.
        /// @param partner a converted slot's other file (`TextureRow::mPartner`), and null for every
        ///        other slot.
        PreparedImage find(
            const osg::Image& file, TextureEncoding encoding, TextureWrap wrap, const osg::Image* partner = nullptr);

        /// Whether nothing is queued or being made: what a run that compares frames waits for, so
        /// every frame it compares draws the made chains.
        bool isIdle();

        /// Waits until `isIdle`.
        void flush();

        /// What every thread's preprocessor cost since the last take, its jobs' whole: a job's
        /// figures join once it is done.
        ContentStats takeStats();

        /// How many asks the table remembers, made or queued: what a test of the sweep reads.
        std::size_t getEntryCount();

    private:
        using Key = std::tuple<const osg::Image*, TextureEncoding, TextureWrap, const osg::Image*>;

        struct Entry
        {
            /// The file, watched so an entry of a file that is gone is never taken for another file
            /// a later allocation put at its address.
            osg::observer_ptr<const osg::Image> mFile;

            /// A converted slot's other file, watched for the same reason; null for every other.
            osg::observer_ptr<const osg::Image> mPartner{};

            bool mMade = false;

            /// Made, and nothing made of it: its own chain stands (`ChainPass`'s empty output).
            bool mStands = false;

            /// Held until first handed out, watched after it.
            osg::ref_ptr<const MadeImage> mHeld{};
            osg::observer_ptr<const MadeImage> mWatched{};
        };

        struct Job
        {
            Key mKey;
            osg::ref_ptr<const osg::Image> mFile;
            osg::ref_ptr<const osg::Image> mPartner{};
        };

        struct Hand;

        void serve(Hand& hand, const std::stop_token& stop);

        /// Whether the entry's file, or a converted slot's partner, is gone.
        static bool isGone(const std::pair<const Key, Entry>& entry);

        /// Drops the entries whose files are gone (`isGone`), once the table holds twice what it held
        /// after the last sweep: a bound that grows with what is live.
        void sweep();

        ContentStore::Store* const mStore;

        Misc::Monitor mMonitor;
        std::map<Key, Entry> mEntries;
        std::size_t mSweepAt = 1024;
        std::deque<Job> mQueue;
        int mInFlight = 0;
        ContentStats mStats;

        /// Each thread's own preprocessor and its thread, last, so the threads are joined before the members they read
        /// go.
        std::vector<std::unique_ptr<Hand>> mHands;
    };
}
