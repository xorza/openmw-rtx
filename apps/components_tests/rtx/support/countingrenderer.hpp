#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <components/rtx/common/index.hpp>
#include <components/rtx/common/rowvector.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/guirenderer.hpp>
#include <components/rtx/renderer/nullrenderer.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/residencyop.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/visibility.h>

#include "firsttexel.hpp"

namespace Rtx::Testing
{
    /// A renderer that records what it was given, over `NullRenderer`, which keeps every promise it does
    /// not record.
    ///
    /// **The decision is what is under test, so nothing here draws.** What a backend stands of residency's
    /// operations has its own tests against a real device (`apps/components_tests/rtxvulkan/`); what nothing else
    /// covers is what a frame hands over, and that answer is the same one on every machine.
    class CountingRenderer final : public Rtx::NullRenderer
    {
    public:
        CountingRenderer()
            : NullRenderer(Rtx::RendererOptions{})
        {
        }

        const Rtx::RenderProfile& getProfile() const override { return mProfile; }

        void standScene(Rtx::SceneSlot slot, const Rtx::SceneDesc& scene) override
        {
            ++mRebuilt;
            mDroppedThrough = scene.textures().getFreedCount();
            NullRenderer::standScene(slot, scene);

            // What the backend does: the array is made again and ends where the scene's table does, whatever it held
            // before.
            countAt(slot) = static_cast<std::uint32_t>(scene.textures().getRows().size());
            heldAt(slot) = { scene.getIdentity(), scene.getStructureRevision() };
        }

        Rtx::DeviceEpoch handOver(
            Rtx::SceneSlot slot, const Rtx::SceneDesc& scene, std::span<const Rtx::ResidencyOp> ops) override
        {
            ++mPlaced;
            if (heldAt(slot).mRevision != scene.getStructureRevision())
                ++mExtended;
            recordStood(ops);
            recordDropped(scene);

            // **The array reaches the end of the scene's table**: a slot the table freed is handed out again, so an
            // arrival can land below the end and lengthen nothing at all.
            countAt(slot) = std::max(countAt(slot), static_cast<std::uint32_t>(scene.textures().getRows().size()));
            heldAt(slot).mRevision = scene.getStructureRevision();
            return NullRenderer::handOver(slot, scene, ops);
        }

        Rtx::SceneHeld describeHeld(Rtx::SceneSlot slot) const override
        {
            const Built& built = heldAt(slot);
            return Rtx::SceneHeld{ .mIdentity = built.mIdentity, .mStructureRevision = built.mRevision };
        }

        /// How long `slot`'s texture array stands: its scene's table.
        std::uint32_t textureCountOf(Rtx::SceneSlot slot) const { return countAt(slot); }

        /// What the device made of a hand-over, which a test fills to be what the next take answers, and empties.
        void takeAnswer(Rtx::SceneSlot, Rtx::DeviceAnswer& into) override { mAnswer.handTo(into); }

        /// What the device has room for, which a test sets to be what the next hand-over reads.
        std::optional<Rtx::ArrivalRoom> getArrivalRoom(Rtx::SceneSlot) const override { return mArrivalRoom; }

        void setUpscale(Upscale upscale) override { mProfile.mUpscale = upscale; }
        Rtx::FrameExtents getExtents() const override { return mExtents; }
        Rtx::FrameTraced renderFrame(const Rtx::FrameRequest&) override
        {
            ++mFrames;
            return {};
        }
        void skipFrame() override { ++mSkipped; }
        std::uint64_t getFrameCount() const override { return mFrames + mSkipped; }

        /// Slots go up, what a texture is sent is kept for a test to read, and nothing is drawn. A
        /// side past `mGuiSide` is refused, as a device refuses one past its own.
        Rtx::GuiSlot addGuiTexture(std::uint32_t width, std::uint32_t height) override
        {
            ++mGuiAsked;
            if (width > mGuiSide || height > mGuiSide)
                return Rtx::GuiSlot::none();
            return Rtx::GuiSlot::at(mGuiTextures++);
        }
        std::span<std::uint8_t> lendGuiTexture(Rtx::GuiSlot, const Rtx::GuiRegion& region) override
        {
            mLent.push_back(region);
            mLending.assign(std::size_t{ region.mWidth } * region.mHeight * 4, 0);
            return mLending;
        }
        void sendGuiTexture(Rtx::GuiSlot) override { ++mGuiSent; }
        void dropGuiTexture(Rtx::GuiSlot) override {}
        void drawGui(std::span<const Rtx::GuiVertex> vertices, std::span<const Rtx::GuiBatch> batches) override
        {
            mGuiVertices.assign(vertices.begin(), vertices.end());
            mGuiBatches.assign(batches.begin(), batches.end());
        }
        bool traceGuiTexture(Rtx::GuiSlot, Rtx::SceneSlot scene, const Rtx::Viewpoint& view,
            const Rtx::GuiTraceOptions& options) override
        {
            mTraced = TracedPicture{ .mScene = scene, .mView = view, .mOptions = options };
            return true;
        }
        void reservePicture(std::uint32_t width, std::uint32_t height) override
        {
            mPictureWidth = std::max(mPictureWidth, width);
            mPictureHeight = std::max(mPictureHeight, height);
        }
        Rtx::SceneSlot addViewScene() override
        {
            // The base's slot, which a hand-over of it reaches.
            const Rtx::SceneSlot slot = NullRenderer::addViewScene();
            ++mViewScenes;
            const std::size_t at = slot.getViewIndex();
            if (at >= mViewTextures.size())
            {
                mViewTextures.resize(at + 1, 0);
                mViewBuilt.resize(at + 1);
            }
            mViewTextures[at] = 0;
            mViewBuilt[at] = Built{};
            return slot;
        }

        /// What a slot was built from, which is what says whether an uploader may append.
        struct Built
        {
            Rtx::SceneIdentity mIdentity{};
            Rtx::StructureRevision mRevision;
        };

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        Built& heldAt(Rtx::SceneSlot slot) { return slot.isWorld() ? mBuilt : mViewBuilt[slot.getViewIndex()]; }
        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        const Built& heldAt(Rtx::SceneSlot slot) const
        {
            return slot.isWorld() ? mBuilt : mViewBuilt[slot.getViewIndex()];
        }

        /// **A table a slot, as a real backend keeps.** An uploader that mixed the world's count
        /// with a doll's would begin one scene's descriptions inside the other's table, which is the
        /// overrun `aSecondSceneOnOneRendererIsBuiltRatherThanAppendedTo` exists for.
        std::uint32_t& countAt(Rtx::SceneSlot slot)
        {
            return slot.isWorld() ? mTextures : mViewTextures[slot.getViewIndex()];
        }

        /// The same for a caller that only reads, so that `describeHeld` needs no cast.
        std::uint32_t countAt(Rtx::SceneSlot slot) const
        {
            return slot.isWorld() ? mTextures : mViewTextures[slot.getViewIndex()];
        }

        /// The texture slots the scene gave up since the last hand-over, read as the backend reads them: once, at
        /// the first extension or placement that sees the scene's count of freed slots move. **The texture array
        /// does not shrink**, which is what `mTextures` staying put records.
        void recordDropped(const Rtx::SceneDesc& scene)
        {
            if (scene.textures().getFreedCount() == mDroppedThrough)
                return;
            const std::span<const Rtx::TextureIndex> freed = scene.textures().getFreed();
            mDropped.insert(mDropped.end(), freed.begin(), freed.end());
            mDroppedThrough = scene.textures().getFreedCount();
        }

        /// What a hand-over stood, from the operations residency issued: each texture stood, with what it was made of.
        void recordStood(std::span<const Rtx::ResidencyOp> ops)
        {
            mDescribedSlots.clear();
            mDescribedSecondTexels.clear();
            mDescribedBytes.clear();
            for (const Rtx::ResidencyOp& op : ops)
            {
                const auto* stand = std::get_if<Rtx::StandImage>(&op);
                if (stand == nullptr || stand->mTexels == nullptr)
                    continue;
                const Rtx::TextureData& texture = *stand->mTexels;
                mDescribedSlots.push_back(stand->mTexture);
                mDescribedBytes.push_back(texture.mBytes.size());
                mDescribedSources.growTo(stand->mTexture, Rtx::TextureSource::StandIn);
                mDescribedSources[stand->mTexture] = texture.mSource;
                mDescribedSecondTexels.push_back(
                    texture.mLevels.size() > 1 ? firstTexelOf(texture.mFormat, texture.bytesOf(1)) : Texel{});
            }
        }

        void dropViewScene(Rtx::SceneSlot slot) override
        {
            mViewDropped.push_back(slot.getViewIndex());
            NullRenderer::dropViewScene(slot);
        }
        bool takeGuiCopy(Rtx::GuiSlot, std::span<std::uint8_t>) override { return false; }
        void readPixels(std::vector<std::uint8_t>&) override {}
        void readDeepPixels(std::vector<std::uint16_t>&) override {}

        /// The first texel of each stood texture's second level, beside `mDescribedSlots`, or nought for one of one
        /// level: what says which chain a slot was stood with.
        std::vector<Texel> mDescribedSecondTexels;

        /// The bytes each described texture hands the device, beside `mDescribedSlots`.
        std::vector<std::uint64_t> mDescribedBytes;

        /// What each slot was last described as, by slot, across every hand-over.
        Rtx::RowVector<Rtx::TextureSource, Rtx::TextureIndex> mDescribedSources;

        /// Which slots the last hand-over stood from texels, in the order it stood them.
        std::vector<Rtx::TextureIndex> mDescribedSlots;

        std::vector<std::uint32_t> mViewTextures;
        std::vector<Built> mViewBuilt;
        Built mBuilt;
        std::uint32_t mViewScenes = 0;

        /// Every view scene given back, in the order it was, so a test can say a slot went back
        /// once and no more.
        std::vector<std::uint32_t> mViewDropped;

        std::uint32_t mPlaced = 0;
        std::uint32_t mExtended = 0;
        std::uint32_t mRebuilt = 0;

        std::uint32_t mTextures = 0;

        /// Every texture slot given back, across every call, in the order it was named, and the scene's count of
        /// freed slots as it last read them.
        std::vector<Rtx::TextureIndex> mDropped;
        std::uint64_t mDroppedThrough = 0;
        std::uint64_t mFrames = 0;
        std::uint64_t mSkipped = 0;

        /// What `takeAnswer` answers, as a device would after a hand-over: what it refused of the meshes, what each
        /// mesh's structure takes, what waits on room, and what each tight copy gives back once it rests.
        Rtx::DeviceAnswer mAnswer;

        /// What `getArrivalRoom` answers: unbounded unless a test bounds it.
        std::optional<Rtx::ArrivalRoom> mArrivalRoom;

        /// What `getProfile` answers, which a test sets to say what the run decided.
        Rtx::RenderProfile mProfile;

        /// Every rectangle of a GUI texture lent, in order, the bytes written into the last one,
        /// and how many were sent back: what a mirror of a picture the game holds sent the device.
        std::vector<Rtx::GuiRegion> mLent;
        std::vector<std::uint8_t> mLending;
        std::uint32_t mGuiSent = 0;

        /// The last frame of interface `drawGui` was handed.
        std::vector<Rtx::GuiVertex> mGuiVertices;
        std::vector<Rtx::GuiBatch> mGuiBatches;

        /// What `getExtents` answers: the size the interface is laid out over.
        Rtx::FrameExtents mExtents;

        /// The largest side a GUI texture is made at, and how many were asked for, made or not.
        std::uint32_t mGuiSide = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t mGuiAsked = 0;

        /// What the last picture inside the interface was traced with, or nothing before the first.
        struct TracedPicture
        {
            Rtx::SceneSlot mScene;
            Rtx::Viewpoint mView;
            Rtx::GuiTraceOptions mOptions;
        };
        std::optional<TracedPicture> mTraced;

        /// The room `reservePicture` was asked for, the largest of every side asked.
        std::uint32_t mPictureWidth = 0;
        std::uint32_t mPictureHeight = 0;

    private:
        std::uint32_t mGuiTextures = 0;
    };
}
