#include "picturetracer.hpp"

#include <cassert>
#include <cmath>
#include <vector>

#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/framesampling.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/timeline.hpp>
#include <components/rtxvulkan/display/displaychain.hpp>
#include <components/rtxvulkan/display/tonepass.hpp>
#include <components/rtxvulkan/gui/guitextures.hpp>
#include <components/rtxvulkan/pipeline/graphicspipeline.hpp>
#include <components/rtxvulkan/scene/devicescene.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>
#include <components/rtxvulkan/shaders/shared/glare.h>
#include <components/rtxvulkan/trace/tracemedia.hpp>
#include <components/rtxvulkan/trace/tracepast.hpp>
#include <components/rtxvulkan/trace/tracerecording.hpp>
#include <components/rtxvulkan/trace/visibilitypass.hpp>

namespace Rtx
{
    PictureTracer::PictureTracer(const Device& device, const TracePasses& passes, const RadianceWidth radiance)
        : mDevice(device)
        , mChain(device, passes, 1, radiance, TracePast::Dropped)
        , mCounts(Buffer::deviceLocal(
              device, sizeof(Shaders::FrameCounts), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "picture counts"))
        , mGlareCounts(Buffer::deviceLocal(
              device, sizeof(Shaders::SunGlareCount), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "picture sun glare counts"))
    {
    }

    namespace
    {
        ImageDescription targetDescription(const VkExtent2D room)
        {
            return ImageDescription{ .mWidth = room.width,
                .mHeight = room.height,
                .mFormat = TonePass::sTargetFormat,
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
        }

        /// What the chain at `room` declares for its arena, which `resize` makes it of and `bytesAt`
        /// measures.
        std::vector<ArenaImage> sharedAt(const VkExtent2D room, const RadianceWidth radiance)
        {
            std::vector<ArenaImage> declared;
            TraceChain::declareShared(room.width, room.height, radiance, declared);
            return declared;
        }
    }

    VkDeviceSize PictureTracer::bytesAt(const Device& device, const VkExtent2D room, const RadianceWidth radiance)
    {
        return TraceChain::bytesAt(device, room.width, room.height, radiance, TracePast::Dropped)
            + ImageArena::bytesFor(device, sharedAt(room, radiance)) + Image::bytesFor(device, targetDescription(room));
    }

    void PictureTracer::resize(const VkExtent2D room)
    {
        if (isAt(room))
            return;

        mArena = ImageArena(mDevice, sharedAt(room, mChain.getRadiance()));
        mChain.resize(room.width, room.height, ArenaRun(mArena, 0, mArena.size()));
        mTarget = Image(MemoryUse::Frame, mDevice, targetDescription(room), "view target");
    }

    void PictureTracer::release()
    {
        mChain.release();
        mArena = ImageArena();
        mTarget = Image();
    }

    void PictureTracer::trace(const GuiSlot texture, const SceneSlot scene, const Shaders::VisibilityConstants& camera,
        const GuiTraceOptions& options, DeviceScene& traced, const RenderProfile& profile, const Parts& parts)
    {
        // The camera's own extent is how much of the texture the picture fills.
        const VkExtent2D extent{ camera.mEyes.mWorld.mWidth, camera.mEyes.mWorld.mHeight };
        assert(holds(extent) && "a picture larger than the chain was grown to");

        // `VisibilityConstants::mTransparentBackground` says why: over the interface the backdrop
        // is the picture's alpha, which is one number, and a medium in front of it asks for three.
        assert((camera.mTransparentBackground == 0
                   || (camera.mFogExtinction == 0.0f && !(camera.mFogEdge > 0.0f) && std::isinf(camera.mWaterLevel)))
            && "a picture laid over the interface that stands in air or water");

        const TraceSubject subject = parts.mMedia.describe(traced, camera, mCounts, mGlareCounts);

        // Nothing puts a picture together across frames, so nothing jitters it, and it has no frame
        // before it.
        const Reconstruction reconstruction = Reconstruction::forPicture();
        Shaders::VisibilityConstants sampled
            = sampleFrame(camera, FrameOptions{}, profile, reconstruction, traced.getCounts(), nullptr);
        traced.measureStars(sampled.mStars);

        // The world's ripple field where the picture is of the world, which is the one place it
        // could have a wake in it; a subject of its own stands in no sea.
        if (scene.isWorld())
            parts.mMedia.placeRipples(sampled);

        Batch trace(mDevice.getPool());
        {
            const VkCommandBuffer commands = trace.getCommands();
            if (subject.mSea)
                parts.mMedia.synthesiseSea(commands, sampled.mWaterTime, nullptr);

            // A doll and a map tile are one frame with no frame before them, so every history says
            // so: the accumulator becomes a pass-through handing on the largest variance there is,
            // which is what tells the cascade to filter as widely as it can.
            const TraceResult picture = mChain.record(commands,
                TraceRecording{
                    .mSubject = subject,
                    .mBinSlot = FrameSlot{},
                    .mAsked = BinCamera::of(camera),
                    .mSampled = sampled,
                    .mReconstruction = reconstruction,
                    .mPastLost = true,
                });

            // The puffs over the picture — a torch's flame in a doll's hand is a sprite — and the
            // curve, and nothing else: a picture is measured off nothing, mapped with no share and
            // spread by no lens, because a map tile is a diagram and the same armour must be the
            // same brightness in two windows.
            parts.mDisplay.record(commands,
                Display{
                    .mTrace = picture,
                    .mShown = picture.mColour,
                    .mUpscaled = false,
                    .mExtent = extent,
                    .mSampled = sampled,
                    .mTarget = mTarget,
                    .mLeftAs = Use::sCopyRead,
                });

            // Borrowed rather than transitioned. Where a GUI texture rests between writes is
            // `GuiTextures`' to say, and a caller that said it here had to keep a barrier's scope in
            // step with the commands below — which it did not. Premultiplied, which is what the
            // curve writes a picture as: `ToneConstants::mBackdrop` says why.
            parts.mTextures.writeWith(
                texture, AlphaForm::Premultiplied, commands, [&](const Image& into, VkImageLayout layout) {
                    assert(extent.width <= into.getWidth() && extent.height <= into.getHeight());

                    // Cleared whole and then covered in part, and only where the picture does not
                    // cover it all: what the trace fills is as much of the texture as the widget is
                    // currently wide, and the rest has to be the clear colour rather than what a wider
                    // picture left there the last time this was drawn. Both are transfer writes to the
                    // same image and nothing orders two of those, so the clear is left as what the copy
                    // meets.
                    if (extent.width < into.getWidth() || extent.height < into.getHeight())
                        into.clear(commands, Use::sTransferWrite,
                            VkClearColorValue{ .float32
                                = { options.mClear[0], options.mClear[1], options.mClear[2], options.mClear[3] } },
                            Use::sCopyWrite);

                    assert(layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                        && "a texture lent in another layout than a copy takes");
                    mTarget.copyTo(commands, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, into, layout, extent);
                });

            if (options.mReadBack)
                parts.mTextures.readBackWith(texture, commands);
        }
        trace.defer();

        // The value the batch rides: the next submit this pool makes, whichever that is. The
        // tables it reads were named the same value as they were handed out above.
        traced.notePictureRide(traced.getSlot(), mDevice.getTimeline().getNext());
    }
}
