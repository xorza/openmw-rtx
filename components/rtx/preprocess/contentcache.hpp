#pragma once

#include <cassert>
#include <concepts>
#include <span>
#include <type_traits>
#include <utility>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/misc/bytebuffer.hpp>

#include "contentcodec.hpp"
#include "contentpass.hpp"

namespace Rtx
{
    /// Where a pass's output is found again by its key instead of computed again, and where a
    /// computed one is kept for the next time the same input arrives — in this session or a later
    /// one: one thread's view of the process's `ContentStore::Store`, through its codecs.
    ///
    /// **It holds nothing where it has no store**, which a test and a picture with none hand it:
    /// every `find` misses and every `keep` is dropped, and no key is made (`holds`).
    class ContentCache
    {
    public:
        /// @param store the process's store, or null for none.
        /// @param reach where an ask may reach: the frame's thread asks memory alone.
        ContentCache(ContentStore::Store* store, ContentStore::Reach reach)
            : mStore(store)
            , mReach(reach)
        {
        }

        /// Whether anything is ever found. While nothing is, no key is made, for an answer that is always no.
        bool holds() const { return mStore != nullptr; }

        /// Fills `output` with what `Pass` computed from `input`, filed under `key`, and says whether
        /// it could. A payload its codec cannot read is a miss, and discarded, so the output run again
        /// is filed in its place; so is one that reads whole and does not fit `input`
        /// (`Pass::fits`), since the digest says a file is intact and not that it is this input's.
        template <ContentPass Pass>
            requires StoredOutput<typename Pass::Output>
        bool find(const ContentStore::Key& key, const typename Pass::Input& input, typename Pass::Output& output)
        {
            if (!mStore->read(key, storeKindOf(Pass::sPass), Pass::sVersion, mReach, mPayload))
                return false;
            if (ContentCodec<typename Pass::Output>::decode(mPayload, output) && fits<Pass>(input, output))
                return true;
            mStore->discard(key, storeKindOf(Pass::sPass));
            return false;
        }

        /// The entry `Pass` filed under `key`, opened for its segments to be read alone (`ContentStore::Store::open`).
        template <ContentPass Pass>
        std::optional<ContentStore::Opened> open(const ContentStore::Key& key)
        {
            return mStore->open(key, storeKindOf(Pass::sPass), Pass::sVersion, mReach);
        }

        /// `ContentStore::Store::readSegments`.
        bool readSegments(
            ContentStore::Opened& entry, std::uint32_t first, std::uint32_t count, std::span<std::byte> into)
        {
            return mStore->readSegments(entry, first, count, into);
        }

        /// Deletes the entry `Pass` filed under `key`, which did not fit its input.
        template <ContentPass Pass>
        void discard(const ContentStore::Key& key)
        {
            mStore->discard(key, storeKindOf(Pass::sPass));
        }

        /// Whether an entry for `Pass` stands under `key`, nothing of it read (`ContentStore::holds`).
        template <ContentPass Pass>
        bool stands(const ContentStore::Key& key)
        {
            return mStore->holds(key, storeKindOf(Pass::sPass), mReach);
        }

        /// Keeps what `Pass` computed from `input`, filed under `key`: encoded into a payload of its
        /// own, which the store's queue takes. Every codec is whole for what its pass hands over, so a
        /// later hit hands over this very output. An output handed over to be kept and not read again
        /// moves into its payload where its codec can take it whole (a made chain's bytes). **A pass that
        /// lays its output in segments says where they end** (`Pass::segmentEndsOf`, a made chain's levels),
        /// which the store keeps a digest each of.
        template <ContentPass Pass, class Output>
            requires StoredOutput<typename Pass::Output>
            && std::same_as<std::remove_cvref_t<Output>, typename Pass::Output>
        void keep(const ContentStore::Key& key, const typename Pass::Input& input, Output&& output)
        {
            ContentStore::Filed entry{ .mKey = key,
                .mKind = storeKindOf(Pass::sPass),
                .mVersion = Pass::sVersion,
                .mPayload = {},
                .mSegmentEnds = {} };
            if constexpr (requires { Pass::segmentEndsOf(input, output, entry.mSegmentEnds); })
                Pass::segmentEndsOf(input, output, entry.mSegmentEnds);
            ContentCodec<typename Pass::Output>::encode(std::forward<Output>(output), entry.mPayload);
            mStore->write(std::move(entry), mReach);
        }

    private:
        /// Whether `output` can be `input`'s, where the pass says what an output must hold of its input.
        template <ContentPass Pass>
        static bool fits(const typename Pass::Input& input, const typename Pass::Output& output)
        {
            if constexpr (requires { Pass::fits(input, output); })
                return Pass::fits(input, output);
            else
                return true;
        }

        ContentStore::Store* mStore;
        ContentStore::Reach mReach;

        /// A payload read, refilled for each, and taken whole by a decoder whose output keeps its bytes
        /// (`ContentCodec<MadeChain>`): so it holds the largest of the small outputs a thread read, and
        /// never a chain.
        Misc::ByteBuffer mPayload;
    };
}
