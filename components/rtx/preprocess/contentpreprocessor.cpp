#include "contentpreprocessor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <utility>

#include <components/contentstore/key.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/rtx/common/clock.hpp>
#include <components/rtx/common/letgo.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/preprocess/texture/chainadmission.hpp>
#include <components/sceneutil/embeddedimage.hpp>

namespace Rtx
{
    template <ContentPass Pass>
    bool ContentPreprocessor::admits(const Pass& pass, const typename Pass::Input& input) const
    {
        if constexpr (requires { Pass::scratchOf(input); })
            return mChainRuns == ChainRuns::Always || admitsChain(Pass::scratchOf(input), input.mImage);
        else
            return true;
    }

    template <ContentPass Pass>
    bool ContentPreprocessor::run(
        Pass& pass, const typename Pass::Input& input, typename Pass::Output& output, const ContentAsk ask)
    {
        PassStats& stats = mStats.at(Pass::sPass);
        ++stats.mAsked;

        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        if (mCache.holds())
        {
            ContentStore::Digest digest(sContentPasses.name(Pass::sPass), Pass::sVersion);
            pass.digest(input, digest);
            const ContentStore::Key key = digest.getKey();
            const std::chrono::steady_clock::time_point keyed = std::chrono::steady_clock::now();

            stats.mKeyMs += since(started, keyed);
            stats.mKeyBytes += digest.getBytes();
            if (mKeysInto != nullptr)
                mKeysInto->push_back(StoredKey{ .mKey = key, .mPass = Pass::sPass });

            if (ask == ContentAsk::Stored ? mCache.stands<Pass>(key) : mCache.find<Pass>(key, input, output))
            {
                ++stats.mHits;
                stats.mFoundMs += since(keyed, std::chrono::steady_clock::now());
                return true;
            }

            if (!admits(pass, input))
                return false;
            pass.run(input, output);
            // A stored ask reads no output, so what the run made moves into the store's payload.
            if (ask == ContentAsk::Stored)
                mCache.keep<Pass>(key, input, std::move(output));
            else
                mCache.keep<Pass>(key, input, output);
            stats.mRunMs += since(keyed, std::chrono::steady_clock::now());
            return true;
        }

        if (!admits(pass, input))
            return false;
        pass.run(input, output);
        stats.mRunMs += since(started, std::chrono::steady_clock::now());
        return true;
    }

    template <ContentPass Pass>
    bool ContentPreprocessor::readPooled(const ContentStore::Key& key, const typename Pass::Input& input,
        BufferPool& pool, const std::stop_token& stop, PooledChain& made)
    {
        std::optional<ContentStore::Opened> opened = mCache.open<Pass>(key);
        if (!opened.has_value())
            return false;

        // **The form's byte, the last segment, first**: it says what the levels are laid as. An entry the store holds
        // in memory is one segment, read whole, its form the last byte.
        const std::uint32_t segments = opened->getSegmentCount();
        const auto entryBytes = static_cast<std::size_t>(opened->endOf(segments - 1));
        const bool whole = segments == 1 && entryBytes > 1;
        const std::uint32_t last = segments - 1;
        std::array<std::byte, 1> form{};
        const std::size_t payloadBytes = entryBytes;
        std::optional<PooledBuffer> buffer;
        if (whole)
        {
            // **Checked against the file's layouts before a buffer is taken for it**: an entry is untrusted, and one
            // that fills no form's layout would otherwise size a take.
            const bool fillsAForm = std::ranges::any_of(
                std::array{ ChainForm::Stands, ChainForm::Bc7, ChainForm::Own }, [&](const ChainForm form) {
                    return fillsLayout(Pass::madeLayout(input, form), form, payloadBytes - 1);
                });
            if (!fillsAForm)
            {
                mCache.discard<Pass>(key);
                return false;
            }
            buffer = pool.take(payloadBytes, stop);
            if (!buffer.has_value() || !mCache.readSegments(*opened, 0, 1, buffer->bytes()))
                return false;
            form[0] = buffer->bytes().back();
            buffer->truncate(payloadBytes - 1);
        }
        else if (opened->endOf(last) - opened->startOf(last) != form.size()
            || !mCache.readSegments(*opened, last, 1, form))
            return false;

        const auto stored = std::to_integer<std::uint8_t>(form[0]);
        const std::size_t levelBytes = payloadBytes - 1;
        if (stored > static_cast<std::uint8_t>(ChainForm::Own)
            || !fillsLayout(
                Pass::madeLayout(input, static_cast<ChainForm>(stored)), static_cast<ChainForm>(stored), levelBytes))
        {
            mCache.discard<Pass>(key);
            return false;
        }

        made.mForm = static_cast<ChainForm>(stored);
        made.mBytes = PooledBuffer();
        if (levelBytes == 0)
            return true;
        if (!whole)
        {
            buffer = pool.take(levelBytes, stop);
            if (!buffer.has_value() || !mCache.readSegments(*opened, 0, last, buffer->bytes()))
                return false;
        }
        made.mBytes = std::move(*buffer);
        return true;
    }

    template <ContentPass Pass>
    bool ContentPreprocessor::runPooled(
        Pass& pass, const typename Pass::Input& input, BufferPool& pool, const std::stop_token& stop, PooledChain& made)
    {
        PassStats& stats = mStats.at(Pass::sPass);
        ++stats.mAsked;

        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        std::optional<ContentStore::Key> key;
        std::chrono::steady_clock::time_point keyed = started;
        if (mCache.holds())
        {
            ContentStore::Digest digest(sContentPasses.name(Pass::sPass), Pass::sVersion);
            pass.digest(input, digest);
            key = digest.getKey();
            keyed = std::chrono::steady_clock::now();
            stats.mKeyMs += since(started, keyed);
            stats.mKeyBytes += digest.getBytes();
            if (mKeysInto != nullptr)
                mKeysInto->push_back(StoredKey{ .mKey = *key, .mPass = Pass::sPass });

            if (readPooled<Pass>(*key, input, pool, stop, made))
            {
                ++stats.mHits;
                stats.mFoundMs += since(keyed, std::chrono::steady_clock::now());
                return true;
            }
            if (stop.stop_requested())
                return false;
        }

        if (!admits(pass, input))
            return false;
        pass.run(input, mMade);
        if (key.has_value())
            mCache.keep<Pass>(*key, input, mMade);

        made.mForm = mMade.mForm;
        made.mBytes = PooledBuffer();
        if (!mMade.mBytes.empty())
        {
            std::optional<PooledBuffer> buffer = pool.take(mMade.mBytes.size(), stop);
            if (!buffer.has_value())
                return false;
            std::copy(mMade.mBytes.begin(), mMade.mBytes.end(), buffer->bytes().begin());
            made.mBytes = std::move(*buffer);
        }
        stats.mRunMs += since(keyed, std::chrono::steady_clock::now());
        return true;
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

    FileFacts ContentPreprocessor::derive(const osg::Image& texels)
    {
        FileFacts derived{ .mFile = hashFile(texels, mTexels), .mFacts = {} };
        mKeys.name(texels, derived.mFile);
        ImageFacts& facts = derived.mFacts;
        facts.mReachesSolid = reachesSolid(texels);
        facts.mMean = meanTexel(texels);
        facts.mAlphaRange = alphaRange(texels);
        facts.mSpecularLayout = specularLayout(texels);
        mKeys.forget();
        return derived;
    }

    bool ContentPreprocessor::chain(const ChainPass::Input& input, MadeChain& made, const ContentAsk ask)
    {
        // A chain takes milliseconds to seconds of a thread, all of it inside the file's decode and the
        // encoders, so a crash there names the file and how it is read.
        const Crash::NoteScope noted("making the chain of \"{}\" as {}", SceneUtil::EmbeddedImage::nameOf(input.mImage),
            sTextureEncodings.name(input.mEncoding));
        return run(mChain, input, made, ask);
    }

    bool ContentPreprocessor::convert(const ConversionPass::Input& input, MadeChain& made, const ContentAsk ask)
    {
        const Crash::NoteScope noted("converting \"{}\" with \"{}\" as {}",
            SceneUtil::EmbeddedImage::nameOf(input.mImage), SceneUtil::EmbeddedImage::nameOf(input.mPartner),
            sTextureEncodings.name(input.mEncoding));
        return run(mConversion, input, made, ask);
    }

    bool ContentPreprocessor::chain(
        const ChainPass::Input& input, BufferPool& pool, const std::stop_token& stop, PooledChain& made)
    {
        const Crash::NoteScope noted("making the chain of \"{}\" as {}", SceneUtil::EmbeddedImage::nameOf(input.mImage),
            sTextureEncodings.name(input.mEncoding));
        return runPooled(mChain, input, pool, stop, made);
    }

    bool ContentPreprocessor::convert(
        const ConversionPass::Input& input, BufferPool& pool, const std::stop_token& stop, PooledChain& made)
    {
        const Crash::NoteScope noted("converting \"{}\" with \"{}\" as {}",
            SceneUtil::EmbeddedImage::nameOf(input.mImage), SceneUtil::EmbeddedImage::nameOf(input.mPartner),
            sTextureEncodings.name(input.mEncoding));
        return runPooled(mConversion, input, pool, stop, made);
    }

    ContentPreprocessor::ContentPreprocessor(
        ContentStore::Store* const store, const ContentStore::Reach reach, const ChainRuns runs)
        : mCache(store, reach)
        , mChainRuns(runs)
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
        letGoOf(mMade.mBytes);
        mChain.letGo();
        mConversion.letGo();
        mBuild.letGo();
    }

    void ContentPreprocessor::letGoTexelScratch()
    {
        letGoOf(mTexels);
    }

    ContentStats ContentPreprocessor::takeStats()
    {
        return std::exchange(mStats, ContentStats{});
    }
}
