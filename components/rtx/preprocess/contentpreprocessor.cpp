#include "contentpreprocessor.hpp"

#include <chrono>
#include <utility>

#include <components/crashcatcher/crashnote.hpp>
#include <components/rtx/common/clock.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/sceneutil/embeddedimage.hpp>

#include <components/contentstore/key.hpp>

namespace Rtx
{
    template <ContentPass Pass>
    void ContentPreprocessor::run(
        Pass& pass, const typename Pass::Input& input, typename Pass::Output& output, const ContentAsk ask)
    {
        PassStats& stats = mStats.at(Pass::sPass);
        ++stats.mAsked;

        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        if constexpr (Pass::sStored)
        {
            if (mCache.holds<Pass>())
            {
                ContentStore::Digest digest(sContentPasses.name(Pass::sPass), Pass::sVersion);
                pass.digest(input, digest);
                const ContentStore::Key key = digest.getKey();
                const std::chrono::steady_clock::time_point keyed = std::chrono::steady_clock::now();

                stats.mKeyMs += since(started, keyed);
                stats.mKeyBytes += digest.getBytes();
                if (mKeysInto != nullptr)
                    mKeysInto->push_back(StoredKey{ .mKey = key, .mPass = Pass::sPass });

                if (ask == ContentAsk::Stored ? mCache.stands(key) : mCache.find<Pass>(key, input, output))
                {
                    // What the key was read from let go of, as a run lets it go: a pass that holds
                    // its input's images between the key and the run says how.
                    if constexpr (requires { pass.release(); })
                        pass.release();
                    ++stats.mHits;
                    stats.mFoundMs += since(keyed, std::chrono::steady_clock::now());
                    return;
                }

                pass.run(input, output);
                // A stored ask reads no output, so what the run made moves into the store's payload.
                if (ask == ContentAsk::Stored)
                    mCache.keep<Pass>(key, std::move(output));
                else
                    mCache.keep<Pass>(key, output);
                stats.mRunMs += since(keyed, std::chrono::steady_clock::now());
                return;
            }
        }

        pass.run(input, output);
        stats.mRunMs += since(started, std::chrono::steady_clock::now());
    }

    void ContentPreprocessor::shape(const ShapePass::Input& input, ShapePass::Output& output)
    {
        run(mShape, input, output);
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

    bool ContentPreprocessor::fallsBelowSolid(const osg::Image& image)
    {
        bool falls = false;
        run(mFall, image, falls);
        return falls;
    }

    AlphaRange ContentPreprocessor::alphaRange(const osg::Image& image)
    {
        AlphaRange range;
        run(mSpan, image, range);
        return range;
    }

    SpecularLayout ContentPreprocessor::specularLayout(const osg::Image& image)
    {
        SpecularLayout layout = SpecularLayout::MetalRoughness;
        run(mKind, image, layout);
        return layout;
    }

    void ContentPreprocessor::chain(const ChainPass::Input& input, MadeChain& made, const ContentAsk ask)
    {
        // A chain takes milliseconds to seconds of a thread, all of it inside the file's decode and the
        // encoders, so a crash there names the file and how it is read.
        const Crash::NoteScope noted("making the chain of \"{}\" as {}, {}",
            SceneUtil::EmbeddedImage::nameOf(input.mImage), sTextureEncodings.name(input.mEncoding),
            sTextureWraps.name(input.mWrap));
        run(mChain, input, made, ask);
    }

    void ContentPreprocessor::convert(const ConversionPass::Input& input, MadeChain& made, const ContentAsk ask)
    {
        const Crash::NoteScope noted("converting \"{}\" with \"{}\" as {}, {}",
            SceneUtil::EmbeddedImage::nameOf(input.mImage), SceneUtil::EmbeddedImage::nameOf(input.mPartner),
            sTextureEncodings.name(input.mEncoding), sTextureWraps.name(input.mWrap));
        run(mConversion, input, made, ask);
    }

    ContentPreprocessor::ContentPreprocessor(ContentStore::Store* const store, const ContentStore::Reach reach)
        : mCache(store, reach)
    {
    }

    ChainScratch ContentPreprocessor::chainScratch() const
    {
        ChainScratch scratch = mConversion.capacity();
        scratch[ChainBuffer::File] = mChain.capacity();
        scratch.setBuild(mBuild.capacity());
        return scratch;
    }

    void ContentPreprocessor::letGoChainScratch()
    {
        mChain.letGo();
        mConversion.letGo();
        mBuild.letGo();
    }

    ContentStats ContentPreprocessor::takeStats()
    {
        return std::exchange(mStats, ContentStats{});
    }
}
