#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <components/rtx/common/index.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/guirenderer.hpp>
#include <components/rtx/renderer/nullrenderer.hpp>
#include <components/rtx/renderer/renderer.hpp>
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
    /// **The decision is what is under test, so nothing here draws.** What `extendScene` and
    /// `setScene` do with the descriptions has its own tests against a real device
    /// (`apps/components_tests/rtxvulkan/trace/visibility/`); what nothing else covers is which of
    /// them a frame picks, and that answer is the same one on every machine.
    class CountingRenderer final : public Rtx::NullRenderer
    {
    public:
        CountingRenderer()
            : NullRenderer(Rtx::RendererOptions{})
        {
        }

        const Rtx::RenderProfile& getProfile() const override { return mProfile; }

        void setScene(
            Rtx::SceneSlot slot, const Rtx::SceneDesc& scene, std::span<const Rtx::TextureData> textures) override
        {
            ++mRebuilt;
            recordSlots(textures);
            mDroppedThrough = scene.textures().getFreedCount();

            // What the backend does: the array is made again and ends where the scene's table
            // does, whatever it held before.
            countAt(slot) = static_cast<std::uint32_t>(scene.textures().getRows().size());
            heldAt(slot) = { scene.getIdentity(), scene.getStructureRevision() };
        }

        void extendScene(
            Rtx::SceneSlot slot, const Rtx::SceneDesc& scene, std::span<const Rtx::TextureData> arrived) override
        {
            ++mExtended;
            recordSlots(arrived);
            recordDropped(scene);

            // **The array reaches the highest slot written, and is not a count of what arrived.**
            // A slot the table freed is handed out again, so an arrival can land below the end and
            // lengthen nothing at all.
            for (const Rtx::TextureData& one : arrived)
                countAt(slot) = std::max(countAt(slot), one.mSlot + 1);

            // The contract `extendScene` is given rather than one it checks: appending only the
            // arrivals has to leave the array exactly as long as the scene's table.
            mAppendedToWrongEnd |= countAt(slot) != scene.textures().getRows().size();
            heldAt(slot).mRevision = scene.getStructureRevision();
        }

        void placeScene(Rtx::SceneSlot, const Rtx::SceneDesc& scene) override
        {
            ++mPlaced;
            recordDropped(scene);
        }

        Rtx::SceneHeld describeHeld(Rtx::SceneSlot slot) const override
        {
            const Built& built = heldAt(slot);
            return Rtx::SceneHeld{ .mIdentity = built.mIdentity, .mStructureRevision = built.mRevision };
        }

        /// How long `slot`'s texture array stands: the highest slot written, plus one.
        std::uint32_t textureCountOf(Rtx::SceneSlot slot) const { return countAt(slot); }

        /// What the device refused, which a test fills to be what the next hand-over answers.
        std::span<const Rtx::Refusal> getRefusals(Rtx::SceneSlot) const override { return mRefusing; }

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
            mViewTextures.push_back(0);
            mViewBuilt.emplace_back();
            return Rtx::SceneSlot::view(mViewScenes++);
        }

        /// What a slot was built from, which is what says whether an uploader may append.
        struct Built
        {
            std::uint64_t mIdentity = 0;
            Rtx::StructureRevision mRevision;
        };

        Built& heldAt(Rtx::SceneSlot slot) { return slot.isWorld() ? mBuilt : mViewBuilt[slot.getViewIndex()]; }
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
            const std::span<const Rtx::Index> freed = scene.textures().getFreed();
            mDropped.insert(mDropped.end(), freed.begin(), freed.end());
            mDroppedThrough = scene.textures().getFreedCount();
        }

        void recordSlots(std::span<const Rtx::TextureData> described)
        {
            mDescribedSlots.clear();
            mDescribedSecondTexels.clear();
            for (const Rtx::TextureData& texture : described)
            {
                mDescribedSlots.push_back(texture.mSlot);
                mDescribedSecondTexels.push_back(
                    texture.mLevels.size() > 1 ? firstTexelOf(texture.mFormat, texture.bytesOf(1)) : Texel{});
            }
        }

        void dropViewScene(Rtx::SceneSlot slot) override { mViewDropped.push_back(slot.getViewIndex()); }
        bool takeGuiCopy(Rtx::GuiSlot, std::span<std::uint8_t>) override { return false; }
        void readPixels(std::vector<std::uint8_t>&) override {}
        void readDeepPixels(std::vector<std::uint16_t>&) override {}

        /// The first texel of each described texture's second level, beside `mDescribedSlots`, or
        /// nought for one of one level: what says which chain a slot was described with.
        std::vector<Texel> mDescribedSecondTexels;

        /// Which slots the last hand-over described, in the order it described them.
        ///
        /// **What says the loader answered about this scene and not the last one.** `SceneTextures`
        /// is held by the uploader and cleared per arrival, so a buffer left unclear would show up
        /// here as a slot belonging to a scene that has gone.
        std::vector<std::uint32_t> mDescribedSlots;

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
        bool mAppendedToWrongEnd = false;

        /// Every texture slot given back, across every call, in the order it was named, and the scene's count of
        /// freed slots as it last read them.
        std::vector<std::uint32_t> mDropped;
        std::uint64_t mDroppedThrough = 0;
        std::uint64_t mFrames = 0;
        std::uint64_t mSkipped = 0;

        /// What `getRefusals` answers, as a device short of room would after a hand-over.
        std::vector<Rtx::Refusal> mRefusing;

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
