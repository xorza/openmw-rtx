#pragma once

#include <components/rtx/preprocess/threadcontent.hpp>

#include "mirrorpass.hpp"

namespace Rtx
{
    class Residency;
    class TextureFeed;

    /// What every walk on one thread shares: where its traversal numbers come from, what it computes from the content,
    /// and what the content's `_spec` maps mean. **One a thread, owned by what owns the thread's walks, and handed to
    /// each by reference**: a walk with a counter or a cache of its own reads another answer than the world's.
    struct WalkContext
    {
        Traversals mTraversals{};
        ThreadContent mContent{};

        /// What decides what stands of a scene this thread hands over, and where its texels come from: the
        /// renderer's, which every scene of it shares, the world's and every picture's.
        Residency* mResidency = nullptr;
        TextureFeed* mFeed = nullptr;
    };
}
