#pragma once

#include "contentpreprocessor.hpp"
#include "imagefactcache.hpp"

namespace Rtx
{
    /// What one thread computes from the content and keeps: the passes and what they cost, and the
    /// facts of every file met. **One a thread, owned by what owns the thread's walks**, so a
    /// picture walked on the frame's thread reads the world's caches and counts into the world's
    /// figures, rather than into a preprocessor of its own that nobody asks and that dies with the
    /// picture. Not movable: the cache reads through the preprocessor it was made with.
    struct ThreadContent
    {
        ContentPreprocessor mPreprocessor;
        ImageFactCache mFacts{ mPreprocessor };
    };
}
