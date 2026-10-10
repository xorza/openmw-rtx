#include "guidrawer.hpp"

#include <array>
#include <cassert>
#include <utility>
#include <vector>

#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/graphicspipeline.hpp>
#include <components/rtxvulkan/present/presenttarget.hpp>

namespace Rtx
{
    GuiDrawer::GuiDrawer(const Device& device)
        : mDevice(device)
        , mPass(device)
        , mTextures(device)
    {
        // Lent once and recorded into again.
        for (std::uint32_t slot = 0; slot < sFrameSlots; ++slot)
        {
            Slot& held = mSlots.at(FrameSlot{ slot });
            held.mCommands = mDevice.getPool().lend(1);
            held.mVertices
                = GrowableBuffer(device, BufferKind::HostWritten, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "gui vertices");
        }
    }

    void GuiDrawer::prepare(std::span<const GuiVertex> vertices, std::span<const GuiBatch> batches)
    {
        // The interface drawn two draws ago drew out of this slot, and the vertices carry the
        // submit that bound them: that passed is what says they may be written over.
        Slot& slot = current();
        slot.mVertices.get().waitIdle("the interface drawn two frames ago");

        // The picture first, over the whole of what is shown and replacing it, and the interface's
        // own vertices after it. White, so the draw hands the picture on as it is.
        constexpr std::uint32_t sWhite = 0xFFFFFFFFu;
        const std::array<GuiVertex, 6> backdrop{
            GuiVertex{ -1.0f, 1.0f, 0.0f, sWhite, 0.0f, 0.0f },
            GuiVertex{ -1.0f, -1.0f, 0.0f, sWhite, 0.0f, 1.0f },
            GuiVertex{ 1.0f, -1.0f, 0.0f, sWhite, 1.0f, 1.0f },
            GuiVertex{ -1.0f, 1.0f, 0.0f, sWhite, 0.0f, 0.0f },
            GuiVertex{ 1.0f, -1.0f, 0.0f, sWhite, 1.0f, 1.0f },
            GuiVertex{ 1.0f, 1.0f, 0.0f, sWhite, 1.0f, 0.0f },
        };
        const auto first = static_cast<std::uint32_t>(backdrop.size());

        slot.mVertices.outgrow(std::span(backdrop).size_bytes() + vertices.size_bytes());
        slot.mVertices.get().write(std::span<const GuiVertex>(backdrop));
        slot.mVertices.get().writeAt(std::span(backdrop).size_bytes(), vertices);

        mDraws.clear();
        mDraws.reserve(batches.size() + 1);
        // The picture's view is filled in as the draw is recorded, against the picture it is
        // recorded over: a target remade between the two would leave a view of an image gone.
        mDraws.push_back(GuiDraw{ .mTexture = VK_NULL_HANDLE,
            .mFirstVertex = 0,
            .mVertexCount = first,
            .mBlend = Blend::None,
            .mSource = AlphaForm::Straight,
            .mLayout = VK_IMAGE_LAYOUT_GENERAL });
        for (const GuiBatch& batch : batches)
        {
            const VkImageView view = mTextures.getView(batch.mTexture);
            assert(view != VK_NULL_HANDLE && "a batch names a texture this renderer does not hold");

            // A slot nothing holds would be a null descriptor, which is undefined rather than
            // blank. The assert above is where a caller finds out; a release build drops the batch.
            if (view != VK_NULL_HANDLE)
                mDraws.push_back(GuiDraw{ view, first + batch.mFirstVertex, batch.mVertexCount,
                    batch.mBlend == GuiBlend::Additive ? Blend::Additive : Blend::Over,
                    mTextures.alphaOf(batch.mTexture) });
        }

        mPrepared = true;
    }

    void GuiDrawer::record(const VkCommandBuffer commands, const Image& picture, const Image& shown)
    {
        assert(mPrepared && "an interface recorded that was never prepared");
        const Slot& slot = current();

        mDraws.front().mTexture = picture.getView();
        picture.transition(commands, PresentTarget::sResting, Use::sFragmentGeneralSample);
        shown.transition(commands, Use::sUndefined, Use::sColourAttachment);
        mPass.record(commands, shown, slot.mVertices.get(), mDraws);

        Barriers rested(commands);
        picture.addTransition(rested, Use::sFragmentGeneralSample, PresentTarget::sResting);
        shown.addTransition(rested, Use::sColourAttachment, PresentTarget::sResting);
        rested.flush();

        mPrepared = false;
        ++mDrawn;
    }

    void GuiDrawer::submit(const Image& picture, const Image& shown)
    {
        Recording recording = mDevice.getPool().begin(current().mCommands[0]);
        record(recording.get(), picture, shown);
        std::move(recording).submit();
    }
}
