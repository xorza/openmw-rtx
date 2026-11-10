#pragma once

#include <cassert>
#include <span>
#include <utility>

#include <components/misc/bytebuffer.hpp>

#include "contentcodec.hpp"
#include "contentpass.hpp"
#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>

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

        /// Whether anything is ever found for `Pass`. While nothing is, no key is made: a key is every
        /// byte of a pass's input read once more, for an answer that is always no.
        template <ContentPass Pass>
        bool holds() const
        {
            return mStore != nullptr && Pass::sStored;
        }

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
            mStore->discard(key);
            return false;
        }

        /// Whether an entry for `Pass` stands under `key`, nothing of it read (`ContentStore::holds`).
        bool stands(const ContentStore::Key& key) { return mStore->holds(key, mReach); }

        /// Keeps what `Pass` computed from the input filed under `key`: encoded into a payload of its
        /// own, which the store's queue takes. Where the codec rounds (`sRounds`), `output` is read back
        /// out of the payload first, so it is what a later hit hands over.
        template <ContentPass Pass>
        requires StoredOutput<typename Pass::Output>
        void keep(const ContentStore::Key& key, typename Pass::Output& output)
        {
            using Codec = ContentCodec<typename Pass::Output>;
            Misc::ByteBuffer payload;
            Codec::encode(output, payload);
            if constexpr (requires { Codec::sRounds; })
            {
                [[maybe_unused]] const bool read = Codec::decode(std::span<const std::byte>(payload), output);
                assert(read && "an output its own codec cannot read back");
            }
            mStore->write(key, storeKindOf(Pass::sPass), Pass::sVersion, std::move(payload), mReach);
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
