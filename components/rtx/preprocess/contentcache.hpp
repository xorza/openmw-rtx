#pragma once

#include <cstddef>
#include <vector>

#include "contentcodec.hpp"
#include "contentkey.hpp"
#include "contentpass.hpp"
#include "contentstore.hpp"

namespace Rtx
{
    /// Where a pass's output is found again by its key instead of computed again, and where a
    /// computed one is kept for the next time the same input arrives — in this session or a later
    /// one: one thread's view of the process's `ContentStore`, through its codecs.
    ///
    /// **It holds nothing where it has no store**, which a test and a picture with none hand it:
    /// every `find` misses and every `keep` is dropped, and no key is made (`holds`).
    class ContentCache
    {
    public:
        /// @param store the process's store, or null for none.
        /// @param reach where an ask may reach: the frame's thread asks memory alone.
        ContentCache(ContentStore* store, StoreReach reach)
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

        /// Fills `output` with what `Pass` computed from the input filed under `key`, and says
        /// whether it could. A payload its codec cannot read is a miss, and discarded, so the output
        /// run again is filed in its place.
        template <ContentPass Pass>
        requires StoredOutput<typename Pass::Output>
        bool find(const ContentKey& key, typename Pass::Output& output)
        {
            if (!mStore->read(key, Pass::sPass, Pass::sVersion, mReach, mPayload))
                return false;
            if (ContentCodec<typename Pass::Output>::decode(mPayload, output))
                return true;
            mStore->discard(key);
            return false;
        }

        /// Keeps what `Pass` computed from the input filed under `key`.
        template <ContentPass Pass>
        requires StoredOutput<typename Pass::Output>
        void keep(const ContentKey& key, const typename Pass::Output& output)
        {
            mPayload.clear();
            ContentCodec<typename Pass::Output>::encode(output, mPayload);
            mStore->write(key, Pass::sPass, Pass::sVersion, mPayload);
        }

    private:
        ContentStore* mStore;
        StoreReach mReach;

        /// A payload read or written, refilled for each.
        std::vector<std::byte> mPayload;
    };
}
