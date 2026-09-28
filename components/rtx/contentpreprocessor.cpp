#include "contentpreprocessor.hpp"

#include <chrono>
#include <utility>

#include "contentkey.hpp"
#include "framespend.hpp"

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

    FoldedShape ContentPreprocessor::fold(const std::span<const osg::Vec3f> positions,
        const std::span<const std::uint32_t> triangles, std::vector<std::uint32_t>& kept)
    {
        ShapeFold::Output folded{ .mKept = kept };
        run(mFold, ShapeFold::Input{ .mPositions = positions, .mIndices = triangles }, folded);
        return folded.mShape;
    }

    bool ContentPreprocessor::reachesSolid(const osg::Image& image)
    {
        bool solid = true;
        run(mSolid, image, solid);
        return solid;
    }

    MeanTexel ContentPreprocessor::meanTexel(const osg::Image& image)
    {
        MeanTexel mean;
        run(mMean, image, mean);
        return mean;
    }

    ContentStats ContentPreprocessor::takeStats()
    {
        return std::exchange(mStats, ContentStats{});
    }
}
