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

        /// The file's texels, which the upload reads the levels nothing made of: every level where
        /// nothing is made, the finest where the made levels are coarser; null where the made image is
        /// whole, or pending.
        osg::ref_ptr<const osg::Image> mFile{};

        /// Whether it is being read or made, so the caller stands it in for now and asks again.
        bool mPending = false;
    };

    /// **What the device is handed of a file, read and made on threads of their own**: its texels, read
    /// through its header where a model holds one (`Resource::ImageHeader`), and its chain made again
    /// from its finest level where it has one (`ChainPass`, `docs/architecture.md`, "A file is read and its
    /// chain built on a worker"). A slot stands in until it is ready, and is handed over once. What is read or made is
    /// held until it is first handed out, and watched after it, so the texture that holds it decides how
    /// long it stays, until the device has its copy (`letGo`). One for a process, shared by every thread
    /// that asks. Pinned: the threads reach every member.
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

        /// What the device is handed of `file`, read as `encoding` by a slot that addresses it as `wrap`:
        /// its texels and its chain made again where `prepares` says, ready, or queued and pending. A file
        /// whose texels it holds and that has no chain made is answered at once. Never waits.
        /// @param partner a converted slot's other file (`TextureRow::mPartner`), and null for every
        ///        other slot.
        PreparedImage find(
            const osg::Image& file, TextureEncoding encoding, TextureWrap wrap, const osg::Image* partner = nullptr);

        /// Whether what `find` queued for `file` as it was asked is read and made, so the slot that stood in
        /// arrives again to be handed it. Hands nothing out.
        bool isReady(const osg::Image& file, TextureEncoding encoding, TextureWrap wrap, const osg::Image* partner);

        /// Lets go of what was read and made of `file` for every slot, the device having its copy, and
        /// tells a header's manager its texels may go (`Resource::releaseTexels`): a later ask reads it
        /// again. What a job reading it holds goes as the job ends.
        void letGo(const osg::Image& file);

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

        /// What every hand keeps of its chains' scratch, asked while `isIdle`: nought, as a hand lets go of
        /// it once the queue is drained. What a test reads.
        std::size_t getScratchBytes();

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

            /// Made, and nothing made of it: its own chain stands (`ChainPass`'s empty output), and for a
            /// file `prepares` refuses.
            bool mStands = false;

            /// Whether the upload reads the file's texels: where nothing is made, or the made levels are
            /// coarser.
            bool mReadsFile = false;

            /// Held until first handed out, watched after it: the made chain, and the file's texels
            /// read through its header.
            osg::ref_ptr<const MadeImage> mHeld{};
            osg::observer_ptr<const MadeImage> mWatched{};
            osg::ref_ptr<const osg::Image> mFileHeld{};
            osg::observer_ptr<const osg::Image> mFileWatched{};
        };

        struct Job
        {
            Key mKey;
            osg::ref_ptr<const osg::Image> mFile;
            osg::ref_ptr<const osg::Image> mPartner{};

            /// Whether its chain is made again (`prepares`), or its texels alone read.
            bool mMakes = false;
        };

        /// The entry `file` is found under as a slot asks for it: the wrap only where it decides what is made.
        static Key keyOf(const osg::Image& file, TextureEncoding encoding, TextureWrap wrap, const osg::Image* partner);

        /// What `entry` answers, made: its chain and its texels as the upload reads them, or pending where
        /// what it held was let go of by every texture and is queued to be read or made again.
        PreparedImage answer(const Key& key, Entry& entry);

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
        std::int32_t mInFlight = 0;
        ContentStats mStats;

        /// Each thread's own preprocessor and its thread, last, so the threads are joined before the members they read
        /// go.
        std::vector<std::unique_ptr<Hand>> mHands;
    };
}
