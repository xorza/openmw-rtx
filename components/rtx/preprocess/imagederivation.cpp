#include "imagederivation.hpp"

#include <cassert>
#include <utility>

#include <osg/Image>

#include <components/contentstore/store.hpp>

#include "headerderived.hpp"

namespace Rtx
{
    ImageDerivation::ImageDerivation(ContentStore::Store* const store, const std::size_t threads)
        : mStore(store)
        , mThreads(threads)
    {
        assert(threads >= 1 && "no preprocessor to derive with");
        mFree.reserve(threads);
    }

    ImageDerivation::~ImageDerivation()
    {
        assert(mFree.size() == mMade && "a derivation outlived by one still deriving");
    }

    osg::ref_ptr<const HeaderDerived> ImageDerivation::derive(const osg::Image& texels)
    {
        std::unique_ptr<ContentPreprocessor> preprocessor;
        {
            std::unique_lock lock(mMutex);
            mFreed.wait(lock, [&] { return !mFree.empty() || mMade < mThreads; });
            if (!mFree.empty())
            {
                preprocessor = std::move(mFree.back());
                mFree.pop_back();
            }
            else
                ++mMade;
        }
        if (preprocessor == nullptr)
            preprocessor = std::make_unique<ContentPreprocessor>(mStore);

        // From the texels in hand, before the manager makes the header: the read is this one.
        const osg::ref_ptr<const HeaderDerived> derived = new HeaderDerived(preprocessor->derive(texels));
        // **The scratch given back after each**, which a pool of idle preprocessors would otherwise hold for the
        // session at the largest file each met.
        preprocessor->letGoTexelScratch();

        const ContentStats stats = preprocessor->takeStats();
        {
            const std::scoped_lock lock(mMutex);
            mStats += stats;
            mFree.push_back(std::move(preprocessor));
        }
        mFreed.notify_one();
        return derived;
    }

    ContentStats ImageDerivation::takeStats()
    {
        const std::scoped_lock lock(mMutex);
        return std::exchange(mStats, ContentStats{});
    }
}
