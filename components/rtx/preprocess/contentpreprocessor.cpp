#include "contentpreprocessor.hpp"

#include <chrono>
#include <utility>

#include <components/rtx/common/clock.hpp>

#include "contentkey.hpp"

namespace Rtx
{
    template <ContentPass Pass>
    void ContentPreprocessor::run(Pass& pass, const typename Pass::Input& input, typename Pass::Output& output)
    {
        PassStats& stats = mStats.at(Pass::sPass);
        ++stats.mAsked;

        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        ContentDigest digest(sContentPasses.name(Pass::sPass), Pass::sVersion);
        pass.digest(input, digest);
        const ContentKey key = digest.getKey();
        const std::chrono::steady_clock::time_point keyed = std::chrono::steady_clock::now();

        stats.mKeyMs += since(started, keyed);
        stats.mKeyBytes += digest.getBytes();

        if (mCache.find<Pass>(key, output))
        {
            ++stats.mHits;
            return;
        }

        pass.run(input, output);
        mCache.keep<Pass>(key, output);
        stats.mRunMs += since(keyed, std::chrono::steady_clock::now());
    }

    void ContentPreprocessor::shape(const ShapePass::Input& input, ShapePass::Output& output)
    {
        run(mShape, input, output);
    }

    ImageFacts ContentPreprocessor::imageFacts(const osg::Image& image)
    {
        ImageFacts facts;
        run(mImageFacts, image, facts);
        return facts;
    }

    ContentStats ContentPreprocessor::takeStats()
    {
        return std::exchange(mStats, ContentStats{});
    }
}
