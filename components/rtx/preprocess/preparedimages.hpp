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
#include <components/rtx/common/bufferpool.hpp>
#include <components/rtx/image/madeimage.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/preprocess/texture/chainscratch.hpp>

#include "contentstats.hpp"
#include "preparedheld.hpp"

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

        /// The file's texels, which the upload reads where nothing is made of it; null where a chain is made,
        /// or pending.
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

        /// Forgets what `find` was asked of `file` as `encoding` and `wrap` by a slot that leaves before it was
        /// handed what was read and made for it: the entry goes now where it is made, and as its job lands where it is
        /// still read or made, unless a slot asks for it again first; and a header's manager may let go of its texels.
        /// Without it the entry would hold its chain and its texels for the session, its file's header being kept
        /// for the run (`Resource::ImageManager`).
        void forget(const osg::Image& file, TextureEncoding encoding, TextureWrap wrap, const osg::Image* partner);

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

        /// What the entries hold that no slot was handed, made chains and files' texels, and how many slots left while
        /// their file was read or made since the table was made (`forget`). A walk of every entry, for a report.
        PreparedHeld getHeld();

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

            /// The slot it was read for left while it was read or made (`forget`): what its job makes goes as it lands.
            bool mLeft = false;

            /// Made, and nothing made of it: its own chain stands (`ChainPass`'s empty output), and for a
            /// file `prepares` refuses. The upload reads the file's texels then, and only then.
            bool mStands = false;

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

            /// Whether its slot left before it started (`forget`), so it reads nothing.
            bool mCancelled = false;
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

        /// **The buffers the made chains are read into, 256 MiB in use and free**: a 4096-square map's whole chain in
        /// BC7 is 21 MiB, so a dozen stand between the hands and the uploads before a hand waits for one given back;
        /// the free ones are the host's for the session, which the handheld can spare no more of. A chain past it is
        /// read alone (`BufferPool`).
        static constexpr std::size_t sPoolBytes = std::size_t{ 256 } << 20;
        std::shared_ptr<BufferPool> mPool;

        Misc::Monitor mMonitor;
        std::map<Key, Entry> mEntries;
        std::size_t mSweepAt = 1024;
        std::deque<Job> mQueue;
        std::int32_t mInFlight = 0;
        ContentStats mStats;
        std::uint64_t mLeftPending = 0;

        /// Each thread's own preprocessor and its thread, last, so the threads are joined before the members they read
        /// go.
        std::vector<std::unique_ptr<Hand>> mHands;
    };
}
