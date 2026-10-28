#include "tracechain.hpp"

#include <algorithm>
#include <cassert>
#include <optional>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/scene/devicescene.hpp>
#include <components/rtxvulkan/scene/scenebuffers.hpp>
#include <components/rtxvulkan/scene/spritesource.hpp>
#include <components/rtxvulkan/shaders/shared/composite.h>
#include <components/rtxvulkan/shaders/shared/fields.h>
#include <components/rtxvulkan/trace/denoise/denoised.hpp>

#include "framestep.hpp"
#include "tracemedia.hpp"
#include "tracepasses.hpp"
#include "tracerecording.hpp"
#include "visibilitypass.hpp"
#include "wavepass.hpp"

namespace Rtx
{
    namespace
    {
        /// What the composite writes the frame into, in its own order and at its own width.
        ImageDescription composedDescription(
            const std::uint32_t width, const std::uint32_t height, const RadianceWidth radiance)
        {
            return ImageDescription{ .mWidth = width,
                .mHeight = height,
                .mFormat = radianceFormat(radiance),
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
        }

        /// The frame's shape, which the display copies the composed frame into and lays the puffs
        /// over: a transfer's target, a storage image and sampled, and read back as the composed
        /// frame is (`Testing::RendererProbe::readShownRadiance`).
        ImageDescription canvasDescription(
            const std::uint32_t width, const std::uint32_t height, const RadianceWidth radiance)
        {
            return ImageDescription{ .mWidth = width,
                .mHeight = height,
                .mFormat = radianceFormat(radiance),
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
        }

        ImageDescription sumDescription(const std::uint32_t width, const std::uint32_t height)
        {
            return ImageDescription{ .mWidth = width,
                .mHeight = height,
                .mFormat = toVulkanFormat(COMPOSITE_SUM_FORMAT),
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT };
        }
    }

    TraceChain::TraceChain(const Device& device, const TracePasses& passes, const std::uint32_t bins,
        const RadianceWidth radiance, const TracePast past)
        : mDevice(device)
        , mPasses(passes)
        , mRadiance(radiance)
        , mPast(past)
        , mDenoise(device, past)
    {
        assert(bins >= 1 && bins <= sFrameSlots && "a sprite bin past the frames in flight");
        mBins.reserve(bins);
        for (std::uint32_t at = 0; at < bins; ++at)
            mBins.emplace_back(device, passes.mSpriteShade, passes.mSpriteBin);
    }

    void TraceChain::resize(const std::uint32_t width, const std::uint32_t height, const ArenaRun& shared)
    {
        assert(width > 0 && height > 0);
        assert(shared.size() == getSharedCount() && "the chain's shared images were not placed as it declared them");

        const ArenaRun stages(shared, 0, GBuffer::sStageImages);
        const ArenaRun scratch(shared, GBuffer::sStageImages, DenoiseHistory::getScratchCount());
        mCanvas = ArenaRun(shared, GBuffer::sStageImages + DenoiseHistory::getScratchCount(), 1);

        // **The one owner of "is this a new extent"**: an upscaling mode changed between two that
        // trace at one size asks this again, and the G-buffer's channels and the fog's images made
        // anew for it would be made for nothing.
        if (isBuilt() && width == mWidth && height == mHeight)
        {
            mChannels->place(mDevice, mPasses.mChannels, stages);
            mDenoise.place(scratch);
            return;
        }

        mWidth = width;
        mHeight = height;

        // **The denoiser's images at the width the fields are stored at**, which is the frame's
        // rounded up to even (`fieldColumns`); each channel at its own (`GBuffer`), and the fog's
        // froxels at the frame's.
        mChannels = std::make_unique<GBuffer>(mDevice, mPasses.mChannels, mWidth, mHeight, mRadiance, mPast, stages);
        mFogVolume = std::make_unique<FogVolume>(mDevice, mPasses.mFog, mWidth, mHeight, mPast);
        mDenoise.resize(Shaders::storedWidth(mWidth), mHeight);
        mDenoise.place(scratch);
        mComposed = Image(MemoryUse::Frame, mDevice, composedDescription(mWidth, mHeight, mRadiance), "composed");

        // Dropped rather than resized, because most runs never make one: sixteen bytes a pixel is
        // worth it to the reference mode and nothing to a window. The first averaging trace asks.
        dropSum();
    }

    void TraceChain::release()
    {
        mWidth = 0;
        mHeight = 0;
        mChannels.reset();
        mFogVolume.reset();
        mDenoise.release();
        mComposed = Image();
        dropSum();
    }

    VkDeviceSize TraceChain::bytesAt(const Device& device, const std::uint32_t width, const std::uint32_t height,
        const RadianceWidth radiance, const TracePast past)
    {
        return GBuffer::bytesAt(device, width, height, radiance, past) + FogVolume::bytesAt(device, width, height, past)
            + DenoiseHistory::bytesAt(device, Shaders::storedWidth(width), height, past)
            + Image::bytesFor(device, composedDescription(width, height, radiance));
    }

    void TraceChain::declareShared(const std::uint32_t width, const std::uint32_t height, const RadianceWidth radiance,
        std::vector<ArenaImage>& into)
    {
        GBuffer::declareStages(width, height, into);
        DenoiseHistory::declareScratch(Shaders::storedWidth(width), height, into);
        into.push_back(ArenaImage{ .mDescription = canvasDescription(width, height, radiance),
            .mSpan = spanOf(FrameStep::Display, FrameStep::Display),
            .mName = "puff canvas" });
    }

    std::size_t TraceChain::getSharedCount()
    {
        return GBuffer::sStageImages + DenoiseHistory::getScratchCount() + 1;
    }

    VkDeviceSize TraceChain::sumBytesAt(const Device& device, const std::uint32_t width, const std::uint32_t height)
    {
        return Image::bytesFor(device, sumDescription(width, height));
    }

    TraceResult TraceChain::record(const VkCommandBuffer commands, const TraceRecording& what)
    {
        assert(isBuilt() && "a trace into a chain that has no extent");
        assert((mPast == TracePast::Kept || (what.mPastLost && what.mSampled.mPrevious.mForward == Shaders::vec3()))
            && "a trace that reads a past, into a chain that keeps one image a pair");

        // Each denoiser's history is worthless until the next trace that reads it, which is only
        // where the wavelet runs: a frame that filters nothing turns every filter fresh in its
        // turn. The air's is read by every trace, and the basis of nothing the frame carries says
        // so to it.
        if (what.mPastLost)
            mDenoise.reset();

        mFogVolume->turn();
        const VisibilityInputs inputs{ .mSubject = what.mSubject, .mChannels = *mChannels, .mFogVolume = *mFogVolume };

        // Made by the first trace that averages, and that trace is the one that fills it: the first
        // write needs no contents and nothing to wait on, and every trace after reads what the last
        // left, which the head barrier `CommandPool::begin` recorded orders and makes visible.
        if (what.mAccumulate > 0 && mSum.isEmpty())
        {
            mSum = Image(MemoryUse::Frame, mDevice, sumDescription(mWidth, mHeight), "sum");
            mSum.transition(commands, Use::sUndefined, Use::sComputeReadWrite);
        }

        assert((!inputs.mSubject.mSea || inputs.mSubject.mMedia->getWaves().holds(what.mSampled.mWaterTime))
            && "a trace of a sea its owner did not synthesise first (`TraceMedia::synthesiseSea`)");

        // The sprite tiles are screen space, so they belong to the camera and not to the scene.
        // Binned on the device into this trace's own bin, ahead of the trace that reads it. **For a
        // camera that draws no sprites as well**, with none of them in it: the bin is also where the
        // tiles learn which media and additive surfaces a ray through them can meet, and a camera
        // told nothing would walk both at every pixel.
        //
        // **Taken, then the block, then the shelter, then the bin.** The block carries the bin's
        // table by address, which it has once the table is taken; the shelter launch reads the
        // block and zeroes the drops under a roof in that table; and the shade and the bin read
        // what is left. Every launch after reads the same block, and the emitters' rows beside it.
        assert(what.mBinSlot.get() < mBins.size() && "a trace slot this chain keeps no bin for");
        SpriteBin& bin = mBins[what.mBinSlot.get()];
        const bool drawsSprites = inputs.mSubject.mDrawsSprites;
        SpriteSource sprites = inputs.mSubject.mScene->getBuffers().describeSprites(inputs.mSubject.mScene->getSlot());
        if (!drawsSprites)
        {
            sprites.mSpriteCount = 0;
            sprites.mEmitterCount = 0;
        }
        bin.take(sprites, what.mAsked.mCamera, commands);

        // After the take, which may have grown the tables, and once: the frame block and the
        // display's `puffsCoverNothing` read the one set.
        const SpriteTables tables = bin.getTables();

        const bool denoised = what.mReconstruction.mDenoised;

        mPasses.mVisibility.writeFrame(commands, inputs, tables, what.mSampled);

        if (drawsSprites)
        {
            mPasses.mVisibility.recordSpriteShelter(commands, inputs, what.mSampled, sprites.mSpriteCount, what.mTimer);
            mPasses.mVisibility.recordSpriteEmitters(commands, inputs, sprites.mEmitterCount, what.mTimer);
        }
        bin.record(commands,
            Binning{
                .mSource = sprites,
                .mSeen = what.mAsked,
                .mTimer = what.mTimer,
            });

        const VisibilityVariant run = mPasses.mVisibility.variantFor(inputs, what.mSampled);
        mChannels->begin(commands, VisibilityPass::writesOf(run, tables.mPuffs));
        mPasses.mVisibility.record(commands, inputs, run, what.mSampled, what.mTimer);
        mChannels->handOver(commands);

        // Where the bounce, the lobe's light and the layers' ended up: the filters' answers, or the
        // channels the trace wrote where nothing filtered them. **An unfiltered frame still turns
        // the histories**, with nothing running, so every filter is fresh at its next run: without
        // the turn, that run would read the history of the frame before this one as last frame's.
        if (!denoised)
            mDenoise.turn(TemporalFlags{}, what.mSampled.mFrame);

        // **A denoised frame nothing sums is composed where the cascade ends**, in its last level,
        // with no level written for the composite to read back (`atrouscompose.comp`). The composite
        // stays for what is left: a frame nothing filtered, and a sum.
        // **Not where water may split a pixel**, whose blend reads its neighbours' light whole,
        // which the cascade's last level holds of no pixel but its own.
        const bool composesInCascade = denoised && what.mAccumulate == 0 && !inputs.mSubject.mSea;
        // Every frame writes it whole, so what the last frame left is discarded; the head barrier
        // `CommandPool::begin` recorded orders this after the last frame's reads.
        mComposed.transition(commands, Use::sUndefined, Use::sComputeWrite);
        const std::optional<Denoised> resolved = denoised
            ? mPasses.mDenoise.record(commands, mDenoise, *mChannels, what.mSampled, inputs.mSubject.mMapped,
                inputs.mSubject.mLamps, composesInCascade, what.mReconstruction, mComposed, what.mTimer)
            : Denoised::unfiltered(*mChannels);
        assert(resolved.has_value() != composesInCascade
            && "the cascade composed where it was not asked, or not where it was");

        // **On every frame the cascade did not compose**, from the channels, whatever filtered them:
        // a trace split into stages holds no pixel's whole light in one place, and the frame returns
        // to its own order here.
        if (resolved.has_value())
            mPasses.mComposite.record(commands, *mChannels, *resolved, mSum.isEmpty() ? nullptr : &mSum,
                Shaders::CompositeConstants{
                    .mWidth = what.mSampled.mEyes.mWorld.mWidth,
                    .mHeight = what.mSampled.mEyes.mWorld.mHeight,
                    .mAccumulate = what.mAccumulate,
                    .mLobed = inputs.mSubject.mMapped ? 1u : 0u,
                    .mFrame = what.mSampled.mFrame,
                },
                mComposed, what.mTimer);

        // Whatever comes next reads what the composite or the cascade just wrote. The frame's scope
        // is the wider of the two — an upscaler, a lens and a curve against a picture's one curve —
        // and covers both. The upscaler's masks with it, which the same pass wrote and an upscaler
        // samples.
        Barriers composed(commands);
        mComposed.addTransition(composed, Use::sComputeWrite, Use::sAnyGeneralRead);
        mChannels->get(Channel::UpscaleMasks).addTransition(composed, Use::sComputeWrite, Use::sAnyShaderRead);
        composed.flush();

        return TraceResult{ .mInputs = inputs,
            .mColour = HandedImage{ .mImage = mComposed, .mLeftAs = Use::sAnyGeneralRead },
            .mCanvas = HandedImage{ .mImage = mCanvas[0], .mLeftAs = mCanvas.firstUseOf(0) },
            .mSprites = tables };
    }
}
