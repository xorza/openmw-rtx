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
    std::optional<ChainAdmission::Ticket> ContentPreprocessor::admits(
        const Pass& /*pass*/, const typename Pass::Input& input)
    {
        if constexpr (requires { Pass::scratchOf(input); })
            if (mAdmission != nullptr)
                return mAdmission->admit(Pass::scratchOf(input), input.mImage);
        return ChainAdmission::Ticket{};
    }

    template <ContentPass Pass, class Found, class Make>
    bool ContentPreprocessor::runCached(Pass& pass, const typename Pass::Input& input, Found&& found, Make&& make)
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

            if (found(*key))
            {
                ++stats.mHits;
                stats.mFoundMs += since(keyed, std::chrono::steady_clock::now());
                return true;
            }
        }

        const std::optional<ChainAdmission::Ticket> admitted = admits(pass, input);
        if (!admitted.has_value())
            return false;
        const bool made = make(key);
        stats.mRunMs += since(keyed, std::chrono::steady_clock::now());
        return made;
    }

    template <ContentPass Pass>
    bool ContentPreprocessor::run(Pass& pass, const typename Pass::Input& input, typename Pass::Output& output)
    {
        return runCached(
            pass, input, [&](const ContentStore::Key& key) { return mCache.find<Pass>(key, input, output); },
            [&](const std::optional<ContentStore::Key>& key) {
                pass.run(input, output);
                if (key.has_value())
                    mCache.keep<Pass>(*key, input, output);
                return true;
            });
    }

    template <ContentPass Pass>
    bool ContentPreprocessor::runStored(Pass& pass, const typename Pass::Input& input)
    {
        // What the run made moves into the store's payload, as nothing reads it here.
        return runCached(
            pass, input, [&](const ContentStore::Key& key) { return mCache.stands<Pass>(key); },
            [&](const std::optional<ContentStore::Key>& key) {
                pass.run(input, mMade);
                if (key.has_value())
                    mCache.keep<Pass>(*key, input, std::move(mMade));
                return true;
            });
    }

    template <ContentPass Pass>
    bool ContentPreprocessor::readPooled(const ContentStore::Key& key, const typename Pass::Input& input,
        const std::uint8_t first, BufferPool& pool, const std::stop_token& stop, PooledChain& made)
    {
        std::optional<ContentStore::Opened> opened = mCache.open<Pass>(key);
        if (!opened.has_value())
            return false;

        // **The form's byte, the last segment, first**: it says what the levels are laid as, a segment each before it.
        const std::uint32_t segments = opened->getSegmentCount();
        const std::uint32_t last = segments - 1;
        std::array<std::byte, 1> form{};
        if (opened->endOf(last) - opened->startOf(last) != form.size())
        {
            mCache.discard<Pass>(key);
            return false;
        }
        if (!mCache.readSegments(*opened, last, 1, form))
            return false;

        const auto stored = std::to_integer<std::uint8_t>(form[0]);
        const auto levelBytes = static_cast<std::size_t>(opened->startOf(last));
        if (stored > static_cast<std::uint8_t>(ChainForm::Own)
            || !fillsLayout(
                Pass::madeLayout(input, static_cast<ChainForm>(stored)), static_cast<ChainForm>(stored), levelBytes))
        {
            mCache.discard<Pass>(key);
            return false;
        }

        made.mForm = static_cast<ChainForm>(stored);
        made.mBytes = PooledBuffer();
        made.mFirst = 0;
        if (levelBytes == 0)
            return true;

        // **A segment a level, read from the first asked**, the finer ones left on the disk: a texture planned coarser
        // than its file reads none of what it leaves out. Its last level at the most.
        const std::uint32_t from = std::min<std::uint32_t>(first, last - 1);
        std::optional<PooledBuffer> buffer
            = pool.take(static_cast<std::size_t>(opened->endOf(last - 1) - opened->startOf(from)), stop);
        if (!buffer.has_value() || !mCache.readSegments(*opened, from, last - from, buffer->bytes()))
            return false;
        made.mFirst = static_cast<std::uint8_t>(from);
        made.mBytes = std::move(*buffer);
        return true;
    }

    template <ContentPass Pass>
    bool ContentPreprocessor::runPooled(Pass& pass, const typename Pass::Input& input, const std::uint8_t first,
        BufferPool& pool, const std::stop_token& stop, PooledChain& made)
    {
        return runCached(
            pass, input,
            [&](const ContentStore::Key& key) { return readPooled<Pass>(key, input, first, pool, stop, made); },
            [&](const std::optional<ContentStore::Key>& key) {
                // A stop asked while the pool waited is no miss to make: what asked it is going.
                if (stop.stop_requested())
                    return false;
                pass.run(input, mMade);
                if (key.has_value())
                    mCache.keep<Pass>(*key, input, mMade);
                made.mForm = mMade.mForm;
                made.mBytes = PooledBuffer();
                made.mFirst = 0;
                if (mMade.mBytes.empty())
                    return true;
                // From the first asked, as a read from the store hands it.
                const std::optional<MadeLayout> layout = Pass::madeLayout(input, mMade.mForm);
                assert(layout.has_value() && "a chain made that fills no layout of its file");
                const std::uint32_t from = std::min<std::uint32_t>(first, layout->mCount - 1);
                const std::size_t start = layout->offsetOf(from);
                std::optional<PooledBuffer> buffer = pool.take(mMade.mBytes.size() - start, stop);
                if (!buffer.has_value())
                    return false;
                std::ranges::copy(std::span(mMade.mBytes).subspan(start), buffer->bytes().begin());
                made.mBytes = std::move(*buffer);
                made.mFirst = static_cast<std::uint8_t>(from);
                return true;
            });
    }

    void ContentPreprocessor::shape(const ShapePass::Input& input, ShapePass::Output& output)
    {
        run(mShape, input, output);
    }

    ImageFacts ContentPreprocessor::facts(const osg::Image& image)
    {
        // Keyed only where there is a store to find the facts in, as every pass is keyed (`runCached`).
        ImageFacts facts;
        run(mFacts,
            FileFactsPass::Input{ .mTexels = image, .mFile = mCache.holds() ? keyOf(image) : ContentStore::Key{} },
            facts);
        return facts;
    }

    FileFacts ContentPreprocessor::derive(const osg::Image& texels)
    {
        FileFacts derived{ .mFile = keyOf(texels), .mFacts = {} };
        run(mFacts, FileFactsPass::Input{ .mTexels = texels, .mFile = derived.mFile }, derived.mFacts);
        return derived;
    }

    ContentStore::Key ContentPreprocessor::keyOf(const osg::Image& texels)
    {
        // Timed into the facts' key figure, as a pass whose digest keys its file counts it in its own.
        const auto started = std::chrono::steady_clock::now();
        const ContentStore::Key key = mKeys.of(texels);
        mStats.at(ContentPassId::FileFacts).mKeyMs
            += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return key;
    }

    bool ContentPreprocessor::prepareChain(const ChainPass::Input& input)
    {
        // A chain takes milliseconds to seconds of a thread, all of it inside the file's decode and the
        // encoders, so a crash there names the file and how it is read.
        const Crash::NoteScope noted("making the chain of \"{}\" as {}", SceneUtil::EmbeddedImage::nameOf(input.mImage),
            sTextureEncodings.name(input.mEncoding));
        return runStored(mChain, input);
    }

    bool ContentPreprocessor::prepareConversion(const ConversionPass::Input& input)
    {
        const Crash::NoteScope noted("converting \"{}\" with \"{}\" as {}",
            SceneUtil::EmbeddedImage::nameOf(input.mImage), SceneUtil::EmbeddedImage::nameOf(input.mPartner),
            sTextureEncodings.name(input.mEncoding));
        return runStored(mConversion, input);
    }

    bool ContentPreprocessor::chain(const ChainPass::Input& input, const std::uint8_t first, BufferPool& pool,
        const std::stop_token& stop, PooledChain& made)
    {
        const Crash::NoteScope noted("making the chain of \"{}\" as {}", SceneUtil::EmbeddedImage::nameOf(input.mImage),
            sTextureEncodings.name(input.mEncoding));
        return runPooled(mChain, input, first, pool, stop, made);
    }

    bool ContentPreprocessor::convert(const ConversionPass::Input& input, const std::uint8_t first, BufferPool& pool,
        const std::stop_token& stop, PooledChain& made)
    {
        const Crash::NoteScope noted("converting \"{}\" with \"{}\" as {}",
            SceneUtil::EmbeddedImage::nameOf(input.mImage), SceneUtil::EmbeddedImage::nameOf(input.mPartner),
            sTextureEncodings.name(input.mEncoding));
        return runPooled(mConversion, input, first, pool, stop, made);
    }

    ContentPreprocessor::ContentPreprocessor(ContentStore::Store* const store, ChainAdmission* const admission)
        : mCache(store)
        , mAdmission(admission)
    {
    }

    ChainScratch ContentPreprocessor::chainScratch() const
    {
        ChainScratch scratch = mConversion.capacity();
        scratch[ChainBuffer::File] = mChain.capacity();
        scratch[ChainBuffer::Made] = mMade.mBytes.capacity();
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
