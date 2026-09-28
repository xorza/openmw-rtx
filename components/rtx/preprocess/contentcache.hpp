#pragma once

#include "contentkey.hpp"
#include "contentpass.hpp"

namespace Rtx
{
    /// Where a pass's output is found again by its key instead of computed again, and where a
    /// computed one is kept for the next time the same input arrives — in this session or a later
    /// one.
    ///
    /// **It holds nothing.** Every `find` misses and every `keep` is dropped, so every pass runs
    /// every time it is asked, as it did before there was a cache. What a store needs of a pass is
    /// its key, made of everything the pass reads, which is made on every ask already, and a way to
    /// write its output and read it back, which arrives with the store because nothing else would
    /// call it.
    class ContentCache
    {
    public:
        /// Fills `output` with what `Pass` computed from the input filed under `key`, and says
        /// whether it could.
        template <ContentPass Pass>
        bool find(const ContentKey& key, typename Pass::Output& output) const
        {
            return false;
        }

        /// Keeps what `Pass` computed from the input filed under `key`.
        template <ContentPass Pass>
        void keep(const ContentKey& key, const typename Pass::Output& output)
        {
        }
    };
}
