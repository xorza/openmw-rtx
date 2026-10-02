#pragma once

#include <components/rtx/preprocess/threadcontent.hpp>
#include <components/rtx/scene/specularlayout.hpp>

#include "mirrorpass.hpp"

namespace Rtx
{
    /// What every walk on one thread shares: where its traversal numbers come from, what it computes
    /// from the content, and what the content's `_spec` maps mean. **One a thread, owned by what owns
    /// the thread's walks, and handed to each by reference**: a walk with a counter or a cache of its
    /// own reads another answer than the world's, and a layout held by each walk that reads one was
    /// five holders, each `Ignore` until told.
    struct WalkContext
    {
        Traversals mTraversals{};
        ThreadContent mContent{};

        /// `[RTX] specular map layout`, told rather than read, so this library reads no settings.
        const SpecularLayout mSpecular;
    };
}
